#include "doc/text_edit.h"

#include "doc/markdown_live.h"

#include <algorithm>

namespace gw::doc {
namespace {

// Vrai si l octet est la continuation d une sequence UTF-8 (10xxxxxx).
inline bool is_continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

// Point de code commencant a `offset`, sans avancer. U+FFFD sur sequence
// invalide : la classification de mot ne doit jamais dependre d une entree
// bien formee.
std::uint32_t codepoint_at(const std::string& text, std::size_t offset) {
    if (offset >= text.size()) return 0;
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return first;

    int extra = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0) == 0xC0) {
        extra = 1;
        value = first & 0x1Fu;
    } else if ((first & 0xF0) == 0xE0) {
        extra = 2;
        value = first & 0x0Fu;
    } else if ((first & 0xF8) == 0xF0) {
        extra = 3;
        value = first & 0x07u;
    } else {
        return 0xFFFD;
    }
    if (offset + static_cast<std::size_t>(extra) >= text.size()) return 0xFFFD;
    for (int i = 1; i <= extra; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + static_cast<std::size_t>(i)]);
        if (!is_continuation(byte)) return 0xFFFD;
        value = (value << 6) | (byte & 0x3Fu);
    }
    return value;
}

// Classification pour les deplacements par mot.
//
// LIMITE ASSUMEE : au-dela de l ASCII, tout point de code est compte comme
// lettre. C est exact pour les alphabets latins accentues, le grec et le
// cyrillique — ce que nous savons afficher — et faux pour la ponctuation
// typographique (guillemets francais, tirets cadratins), qui se retrouve
// collee au mot voisin. La table Unicode complete des proprietes de coupure de
// mot pese plus que tout ce module ; elle viendra avec le shaping, ou jamais.
bool is_word_codepoint(std::uint32_t cp) {
    if (cp >= 0x80) return true;
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           cp == '_';
}

bool is_space_codepoint(std::uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x00A0; // espace insecable comprise
}

} // namespace

// --- Positions --------------------------------------------------------------

bool operator==(const Position& a, const Position& b) {
    return a.paragraph == b.paragraph && a.offset == b.offset;
}
bool operator!=(const Position& a, const Position& b) { return !(a == b); }
bool operator<(const Position& a, const Position& b) {
    if (a.paragraph != b.paragraph) return a.paragraph < b.paragraph;
    return a.offset < b.offset;
}

// --- Frontieres UTF-8 -------------------------------------------------------

std::size_t previous_char_boundary(const std::string& text, std::size_t offset) {
    if (offset == 0) return 0;
    std::size_t i = std::min(offset, text.size());
    --i;
    // Au plus trois octets de continuation a franchir : la borne evite de
    // remonter tout le paragraphe sur une entree mal formee.
    int guard = 3;
    while (i > 0 && guard-- > 0 && is_continuation(static_cast<unsigned char>(text[i]))) --i;
    return i;
}

std::size_t next_char_boundary(const std::string& text, std::size_t offset) {
    if (offset >= text.size()) return text.size();
    std::size_t i = offset + 1;
    int guard = 3;
    while (i < text.size() && guard-- > 0 && is_continuation(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    return i;
}

// --- Construction et etat ---------------------------------------------------

TextEdit::TextEdit() { reset(Document{}); }

void TextEdit::reset(Document document) {
    document_ = std::move(document);
    // Un document sans aucun paragraphe n a pas de position valide ou poser le
    // curseur. Le paragraphe vide est l equivalent du document vierge.
    if (document_.paragraphs().empty()) document_.paragraphs().push_back(Paragraph{});

    caret_ = Position{};
    anchor_ = Position{};
    pending_style_.reset();
    undo_.clear();
    redo_.clear();
    paragraph_snapshots_.clear();
    last_kind_ = EditKind::None;
    last_edit_end_ = Position{};
}

const std::string& TextEdit::text_at(std::size_t paragraph) const {
    document_.paragraphs()[paragraph].ensure_decompressed();
    return document_.paragraphs()[paragraph].text;
}

Position TextEdit::clamp(Position position) const {
    const auto& paragraphs = document_.paragraphs();
    if (position.paragraph >= paragraphs.size()) {
        position.paragraph = paragraphs.size() - 1;
        paragraphs[position.paragraph].ensure_decompressed();
        position.offset = paragraphs[position.paragraph].text.size();
        return position;
    }
    paragraphs[position.paragraph].ensure_decompressed();
    const std::string& text = paragraphs[position.paragraph].text;
    if (position.offset > text.size()) {
        position.offset = text.size();
    } else if (position.offset > 0 &&
               is_continuation(static_cast<unsigned char>(text[position.offset]))) {
        // Position tombee au milieu d une sequence : on recule sur la
        // frontiere. Reculer plutot qu avancer garde le curseur dans le mot ou
        // l appelant visait.
        position.offset = previous_char_boundary(text, position.offset);
    }
    return position;
}

Position TextEdit::selection_begin() const { return caret_ < anchor_ ? caret_ : anchor_; }
Position TextEdit::selection_end() const { return caret_ < anchor_ ? anchor_ : caret_; }

std::string TextEdit::selected_text() const {
    if (!has_selection()) return {};
    const Position begin = selection_begin();
    const Position end = selection_end();

    if (begin.paragraph == end.paragraph) {
        return text_at(begin.paragraph).substr(begin.offset, end.offset - begin.offset);
    }

    std::string out = text_at(begin.paragraph).substr(begin.offset);
    for (std::size_t i = begin.paragraph + 1; i < end.paragraph; ++i) {
        out += '\n';
        out += text_at(i);
    }
    out += '\n';
    out += text_at(end.paragraph).substr(0, end.offset);
    return out;
}

void TextEdit::set_caret(Position position, bool extend) {
    caret_ = clamp(position);
    if (!extend) anchor_ = caret_;
    // Un deplacement desarme le style en attente : une bascule Gras suivie
    // d un clic ailleurs ne doit pas ecrire en gras a l autre bout du document.
    pending_style_.reset();
    // Un deplacement coupe la continuite de frappe : taper, bouger, retaper
    // doit faire deux annulations distinctes, sinon Ctrl+Z effacerait du texte
    // que l utilisateur n a pas saisi d un seul geste.
    last_kind_ = EditKind::None;
}

void TextEdit::select_all() {
    anchor_ = Position{0, 0};
    const std::size_t last = document_.paragraphs().size() - 1;
    caret_ = Position{last, text_at(last).size()};
    last_kind_ = EditKind::None;
}

void TextEdit::select_word_at(Position position) {
    position = clamp(position);
    const std::string& text = text_at(position.paragraph);
    if (text.empty()) {
        set_caret(position, false);
        return;
    }

    // Un double-clic juste apres le dernier caractere doit prendre le mot qui
    // precede, pas ne rien prendre.
    std::size_t probe = position.offset;
    if (probe >= text.size()) probe = previous_char_boundary(text, text.size());

    const std::uint32_t cp = codepoint_at(text, probe);
    const bool want_word = is_word_codepoint(cp);
    const bool want_space = is_space_codepoint(cp);

    auto matches = [&](std::uint32_t other) {
        if (want_word) return is_word_codepoint(other);
        if (want_space) return is_space_codepoint(other);
        // Ponctuation : on ne prend que le caractere lui-meme. Etendre a la
        // ponctuation voisine selectionnerait « ... » ou « ), » d un bloc, ce
        // qui n aide personne.
        return false;
    };

    std::size_t begin = probe;
    while (begin > 0) {
        const std::size_t previous = previous_char_boundary(text, begin);
        if (!matches(codepoint_at(text, previous))) break;
        begin = previous;
    }
    std::size_t end = next_char_boundary(text, probe);
    while (end < text.size() && matches(codepoint_at(text, end))) {
        end = next_char_boundary(text, end);
    }

    anchor_ = Position{position.paragraph, begin};
    caret_ = Position{position.paragraph, end};
    last_kind_ = EditKind::None;
}

// --- Deplacements -----------------------------------------------------------

Position TextEdit::position_left(Position from, bool by_word) const {
    from = clamp(from);
    if (from.offset == 0) {
        // Debut de paragraphe : on passe a la fin du precedent, ce qui traduit
        // le fait que la coupure de paragraphe est un caractere comme un autre
        // du point de vue du deplacement.
        if (from.paragraph == 0) return from;
        const std::size_t previous = from.paragraph - 1;
        return Position{previous, text_at(previous).size()};
    }

    const std::string& text = text_at(from.paragraph);
    if (!by_word) return Position{from.paragraph, previous_char_boundary(text, from.offset)};

    // Par mot : on saute d abord les separateurs, puis le mot lui-meme. Dans
    // cet ordre, Ctrl+Gauche depuis la fin de « mot   » atteint bien le debut
    // de « mot » et non l espace qui le suit.
    std::size_t offset = from.offset;
    while (offset > 0) {
        const std::size_t previous = previous_char_boundary(text, offset);
        if (is_word_codepoint(codepoint_at(text, previous))) break;
        offset = previous;
    }
    while (offset > 0) {
        const std::size_t previous = previous_char_boundary(text, offset);
        if (!is_word_codepoint(codepoint_at(text, previous))) break;
        offset = previous;
    }
    return Position{from.paragraph, offset};
}

Position TextEdit::position_right(Position from, bool by_word) const {
    from = clamp(from);
    const std::string& text = text_at(from.paragraph);
    if (from.offset >= text.size()) {
        if (from.paragraph + 1 >= document_.paragraphs().size()) return from;
        return Position{from.paragraph + 1, 0};
    }

    if (!by_word) return Position{from.paragraph, next_char_boundary(text, from.offset)};

    std::size_t offset = from.offset;
    while (offset < text.size() && is_word_codepoint(codepoint_at(text, offset))) {
        offset = next_char_boundary(text, offset);
    }
    while (offset < text.size() && !is_word_codepoint(codepoint_at(text, offset))) {
        offset = next_char_boundary(text, offset);
    }
    return Position{from.paragraph, offset};
}

void TextEdit::move_left(bool extend, bool by_word) {
    // Sans Maj, une selection existante se replie sur son bord gauche au lieu
    // de deplacer le curseur : c est ce que fait tout editeur, et l ignorer se
    // remarque immediatement a l usage.
    if (!extend && has_selection()) {
        set_caret(selection_begin(), false);
        return;
    }
    set_caret(position_left(caret_, by_word), extend);
}

void TextEdit::move_right(bool extend, bool by_word) {
    if (!extend && has_selection()) {
        set_caret(selection_end(), false);
        return;
    }
    set_caret(position_right(caret_, by_word), extend);
}

void TextEdit::move_paragraph_start(bool extend) {
    set_caret(Position{caret_.paragraph, 0}, extend);
}

void TextEdit::move_paragraph_end(bool extend) {
    set_caret(Position{caret_.paragraph, text_at(caret_.paragraph).size()}, extend);
}

void TextEdit::move_document_start(bool extend) { set_caret(Position{0, 0}, extend); }

void TextEdit::move_document_end(bool extend) {
    const std::size_t last = document_.paragraphs().size() - 1;
    set_caret(Position{last, text_at(last).size()}, extend);
}

// --- Historique -------------------------------------------------------------

TextEdit::Snapshot TextEdit::capture() const {
    const auto& current_pars = document_.paragraphs();
    if (paragraph_snapshots_.size() != current_pars.size()) {
        paragraph_snapshots_.resize(current_pars.size());
    }
    for (std::size_t i = 0; i < current_pars.size(); ++i) {
        if (!paragraph_snapshots_[i] || paragraph_snapshots_[i]->version != current_pars[i].version) {
            paragraph_snapshots_[i] = std::make_shared<const Paragraph>(current_pars[i]);
        }
    }
    return Snapshot{paragraph_snapshots_, caret_, anchor_, pending_style_,
                    document_.default_font()};
}

void TextEdit::restore(const Snapshot& snapshot) {
    document_.paragraphs().resize(snapshot.paragraphs.size());
    for (std::size_t i = 0; i < snapshot.paragraphs.size(); ++i) {
        document_.paragraphs()[i] = *snapshot.paragraphs[i];
    }
    paragraph_snapshots_ = snapshot.paragraphs;
    caret_ = clamp(snapshot.caret);
    anchor_ = clamp(snapshot.anchor);
    pending_style_ = snapshot.pending_style;
    document_.set_default_font(snapshot.default_font);
}

void TextEdit::push_undo(EditKind kind) {
    // CHOIX : instantane complet du document plutot que journal d operations
    // inversibles. Un journal serait plus economique en memoire, mais chaque
    // nouvelle operation devrait fournir son inverse exact — c est la source
    // classique de bugs d annulation, ou l on retrouve un document legerement
    // different apres un aller-retour. Ici l aller-retour est exact par
    // construction. Le cout est celui d une copie de quelques dizaines de Kio
    // sur une note reelle, uniquement au moment d une mutation.
    redo_.clear();

    // Frappe ou effacement continu : on ne rouvre pas d entree. La continuite
    // est jugee sur la position, pas seulement sur la nature de l operation,
    // pour qu un clic ailleurs entre deux frappes coupe bien le groupe.
    const bool continues = kind != EditKind::Other && kind == last_kind_ &&
                           caret_ == last_edit_end_ && !undo_.empty();
    if (!continues) {
        undo_.push_back(capture());
        if (undo_.size() > kMaxUndoDepth) undo_.erase(undo_.begin());
    }
    last_kind_ = kind;
}

bool TextEdit::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(capture());
    restore(undo_.back());
    undo_.pop_back();
    document_.set_dirty(true);
    last_kind_ = EditKind::None;
    return true;
}

bool TextEdit::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(capture());
    restore(redo_.back());
    redo_.pop_back();
    document_.set_dirty(true);
    last_kind_ = EditKind::None;
    return true;
}

// --- Mutations --------------------------------------------------------------

bool TextEdit::delete_selection() {
    if (!has_selection()) return false;
    const Position begin = selection_begin();
    const Position end = selection_end();
    auto& paragraphs = document_.paragraphs();

    bool touches_table = false;
    for (std::size_t i = begin.paragraph; i <= end.paragraph; ++i) {
        touches_table = touches_table || paragraphs[i].in_table();
    }

    if (begin.paragraph == end.paragraph) {
        paragraph_erase(paragraphs[begin.paragraph], begin.offset, end.offset - begin.offset);
    } else if (touches_table) {
        // Une cellule ne fusionne jamais avec sa voisine : la selection vide le
        // texte de chaque paragraphe qu elle couvre, sans retirer aucun
        // paragraphe. Les cellules restent donc en place, meme vides.
        for (std::size_t i = begin.paragraph; i <= end.paragraph; ++i) {
            Paragraph& p = paragraphs[i];
            p.ensure_decompressed();
            const std::size_t from = i == begin.paragraph ? begin.offset : 0;
            const std::size_t to = i == end.paragraph ? end.offset : p.text.size();
            if (to > from) paragraph_erase(p, from, to - from);
        }
    } else {
        // Les paragraphes intermediaires disparaissent, et les deux moities
        // survivantes fusionnent en un seul paragraphe. Le survivant est celui
        // du HAUT : il garde son niveau de titre, son alignement et son type de
        // liste, ceux du bas sont perdus. C est la regle de toutes les fusions
        // du modele (cf. paragraph_append).
        Paragraph tail = paragraphs[end.paragraph];
        tail.ensure_decompressed();
        paragraph_erase(tail, 0, end.offset);

        Paragraph& head = paragraphs[begin.paragraph];
        head.ensure_decompressed();
        paragraph_erase(head, begin.offset, head.text.size() - begin.offset);
        paragraph_append(head, tail);

        paragraphs.erase(paragraphs.begin() + static_cast<std::ptrdiff_t>(begin.paragraph) + 1,
                         paragraphs.begin() + static_cast<std::ptrdiff_t>(end.paragraph) + 1);
    }

    caret_ = begin;
    anchor_ = begin;
    document_.set_dirty(true);
    return true;
}

void TextEdit::insert_text(const std::string& utf8) {
    if (utf8.empty()) return;
    // Remplacer une selection n est pas de la frappe continue : le groupe
    // d annulation en cours doit se fermer, sinon Ctrl+Z ne rendrait pas le
    // texte qui vient d etre ecrase.
    push_undo(has_selection() ? EditKind::Other : EditKind::Typing);
    delete_selection();

    // Dans une cellule, pas de saut de ligne : il devient un espace, sinon il
    // couperait la cellule en deux paragraphes et casserait la grille.
    std::string flat;
    const std::string* source = &utf8;
    if (caret_in_table()) {
        flat = utf8;
        for (char& c : flat) {
            if (c == '\n') c = ' ';
        }
        source = &flat;
    }
    const std::string& text_in = *source;

    // Un texte colle porte ses propres fins de ligne : chaque LF devient une
    // coupure de paragraphe. Sans ce traitement, un collage multiligne
    // insererait des octets 0x0A au milieu d un paragraphe, invisibles a
    // l ecran mais bien ecrits dans le fichier.
    //
    // Style impose par une bascule armee sans selection, sinon nul : le texte
    // insere herite alors du caractere qui le precede (cf. paragraph_insert).
    const CharStyle* forced = pending_style_ ? &*pending_style_ : nullptr;

    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text_in.find('\n', start);
        const std::string chunk =
            text_in.substr(start, newline == std::string::npos ? std::string::npos : newline - start);

        if (!chunk.empty()) {
            paragraph_insert(document_.paragraphs()[caret_.paragraph], caret_.offset, chunk,
                             forced);
            caret_.offset += chunk.size();
        }
        if (newline == std::string::npos) break;

        auto& paragraphs = document_.paragraphs();
        Paragraph tail = paragraph_split(paragraphs[caret_.paragraph], caret_.offset);
        paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(caret_.paragraph) + 1,
                          std::move(tail));
        caret_ = Position{caret_.paragraph + 1, 0};
        start = newline + 1;
    }

    // Le style arme est consomme : la suite de la frappe herite naturellement
    // du texte qui vient d etre pose, donc le garder arme ne changerait rien
    // tant que le curseur ne bouge pas, et le laisser trainer serait un etat de
    // plus a invalider.
    pending_style_.reset();

    anchor_ = caret_;
    last_edit_end_ = caret_;
    document_.set_dirty(true);
}

void TextEdit::insert_blocks(std::vector<Paragraph> blocks) {
    if (blocks.empty()) return;

    // Dans une cellule, un collage ne peut pas creer de paragraphes : on ne
    // garde que le texte, a plat, separe par des espaces.
    if (caret_in_table()) {
        std::string joined;
        for (const Paragraph& block : blocks) {
            block.ensure_decompressed();
            if (block.text.empty()) continue;
            if (!joined.empty()) joined += ' ';
            joined += block.text;
        }
        insert_text(joined);
        return;
    }

    // Meme regle que insert_text : un collage ferme le groupe d annulation.
    push_undo(EditKind::Other);
    delete_selection();

    auto& paragraphs = document_.paragraphs();
    // La queue, c est ce qui suivait le curseur dans sa ligne. Elle est recollee
    // au dernier bloc a la fin, donc elle ne garde pas ses attributs.
    Paragraph tail = paragraph_split(paragraphs[caret_.paragraph], caret_.offset);

    // Une ligne vide ou le curseur se trouve en debut de paragraphe est remplacee
    // par les blocs, sans laisser une ligne vide en trop au-dessus.
    std::size_t first = caret_.paragraph + 1;
    if (paragraphs[caret_.paragraph].text.empty()) {
        paragraphs.erase(paragraphs.begin() + static_cast<std::ptrdiff_t>(caret_.paragraph));
        first = caret_.paragraph;
    }

    // Les tableaux collés gardent leur identifiant, rebase sur le document :
    // deux tableaux distincts ne doivent jamais partager le meme `table_id`.
    std::uint32_t id_offset = next_table_id(paragraphs) - 1;
    for (Paragraph& block : blocks) {
        if (block.in_table()) block.table_id += id_offset;
    }

    const std::size_t count = blocks.size();
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(first),
                      std::make_move_iterator(blocks.begin()),
                      std::make_move_iterator(blocks.end()));

    const std::size_t last = first + count - 1;
    if (paragraphs[last].in_table()) {
        // La queue ne peut pas se recoller dans une cellule : elle devient un
        // paragraphe a part, apres le tableau.
        Paragraph rest = std::move(tail);
        rest.heading = HeadingLevel::None;
        rest.list = ListKind::None;
        rest.align = Alignment::Left;
        paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(last) + 1,
                          std::move(rest));
        caret_ = Position{last + 1, 0};
        anchor_ = caret_;
        pending_style_.reset();
        last_edit_end_ = caret_;
        document_.set_dirty(true);
        return;
    }

    Paragraph& tail_host = paragraphs[last];
    const std::size_t join = tail_host.text.size();
    paragraph_append(tail_host, tail);

    caret_ = Position{last, join};
    anchor_ = caret_;
    pending_style_.reset();
    last_edit_end_ = caret_;
    document_.set_dirty(true);
}

void TextEdit::insert_paragraph_break() {
    if (caret_in_table()) {
        // Entree dans une cellule passe a la suivante, sans couper la cellule.
        push_undo(EditKind::Other);
        delete_selection();
        move_cell(false);
        last_edit_end_ = caret_;
        return;
    }

    // EditKind::Other : une nouvelle ligne ferme le groupe d annulation en
    // cours. Ctrl+Z apres un paragraphe tape entier remonte au debut de ce
    // paragraphe, pas au debut du texte.
    push_undo(EditKind::Other);
    delete_selection();

    auto& paragraphs = document_.paragraphs();
    // Le paragraphe cree herite du niveau de titre, du type de liste et de
    // l alignement de celui qu on coupe : couper une puce en deux donne deux
    // puces, et couper un titre au milieu donne deux titres. La regle « la
    // ligne qui suit un titre repasse en corps de texte » est un confort de
    // saisie qui appartient au chantier clavier-first, pas au modele.
    Paragraph tail = paragraph_split(paragraphs[caret_.paragraph], caret_.offset);
    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(caret_.paragraph) + 1,
                      std::move(tail));

    caret_ = Position{caret_.paragraph + 1, 0};
    anchor_ = caret_;
    last_edit_end_ = caret_;
    document_.set_dirty(true);
}

void TextEdit::erase_backward(bool by_word) {
    if (has_selection()) {
        push_undo(EditKind::Other);
        delete_selection();
        last_edit_end_ = caret_;
        return;
    }

    // Debut de cellule : rien a effacer, la cellule precedente reste intacte.
    if (caret_in_table() && caret_.offset == 0) return;

    const Position target = position_left(caret_, by_word);
    if (target == caret_) return; // debut du document : rien a effacer
    // La frontiere d un tableau ne s efface pas au clavier : on ne fusionne
    // jamais une cellule avec le paragraphe qui la precede, ni l inverse.
    if (crosses_table(target)) return;

    push_undo(by_word ? EditKind::Other : EditKind::Deleting);
    anchor_ = target;
    delete_selection();
    last_edit_end_ = caret_;
}

void TextEdit::erase_forward(bool by_word) {
    if (has_selection()) {
        push_undo(EditKind::Other);
        delete_selection();
        last_edit_end_ = caret_;
        return;
    }

    // Fin de cellule : la suppression ne rapproche pas la cellule suivante.
    if (caret_in_table() && caret_.offset >= text_at(caret_.paragraph).size()) return;

    const Position target = position_right(caret_, by_word);
    if (target == caret_) return;
    if (crosses_table(target)) return;

    push_undo(by_word ? EditKind::Other : EditKind::Deleting);
    anchor_ = target;
    delete_selection();
    last_edit_end_ = caret_;
}

// --- Tableaux ---------------------------------------------------------------

bool TextEdit::crosses_table(Position target) const {
    if (target.paragraph == caret_.paragraph) return false;
    const auto& paragraphs = document_.paragraphs();
    return paragraphs[target.paragraph].in_table() || caret_in_table();
}

bool TextEdit::caret_in_table() const {
    return caret_.paragraph < document_.paragraphs().size() &&
           document_.paragraphs()[caret_.paragraph].in_table();
}

void TextEdit::insert_table(std::size_t rows, std::size_t cols) {
    if (rows == 0 || cols == 0) return;
    push_undo(EditKind::Other);
    delete_selection();

    auto& paragraphs = document_.paragraphs();
    const std::uint32_t id = next_table_id(paragraphs);

    std::vector<Paragraph> block;
    block.reserve(rows * cols + 1);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            Paragraph cell;
            cell.table_id = id;
            cell.table_row = static_cast<std::uint32_t>(r);
            cell.table_col = static_cast<std::uint32_t>(c);
            cell.table_cols = static_cast<std::uint32_t>(cols);
            block.push_back(std::move(cell));
        }
    }

    // Le tableau se place apres le paragraphe du curseur, ou apres le tableau
    // qui le contient. Dans le premier cas, la queue de la ligne passe sous le
    // tableau, comme pour un saut de ligne.
    std::size_t at;
    Paragraph tail;
    if (caret_in_table()) {
        at = table_end(paragraphs, caret_.paragraph);
    } else {
        tail = paragraph_split(paragraphs[caret_.paragraph], caret_.offset);
        tail.heading = HeadingLevel::None;
        tail.list = ListKind::None;
        tail.align = Alignment::Left;
        at = caret_.paragraph + 1;
    }
    // Toujours un paragraphe apres le tableau : sans lui, le curseur n aurait
    // nulle part ou aller en sortant de la derniere cellule.
    block.push_back(std::move(tail));

    paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(at),
                      std::make_move_iterator(block.begin()),
                      std::make_move_iterator(block.end()));

    caret_ = Position{at, 0};
    anchor_ = caret_;
    pending_style_.reset();
    last_edit_end_ = caret_;
    document_.set_dirty(true);
}

bool TextEdit::move_cell(bool backward) {
    if (!caret_in_table()) return false;
    auto& paragraphs = document_.paragraphs();
    const std::size_t begin = table_begin(paragraphs, caret_.paragraph);
    const std::size_t end = table_end(paragraphs, caret_.paragraph);
    const std::uint32_t cols = paragraphs[caret_.paragraph].table_cols;

    if (backward) {
        if (caret_.paragraph == begin) return false;
        caret_ = Position{caret_.paragraph - 1, 0};
    } else if (caret_.paragraph + 1 < end) {
        caret_ = Position{caret_.paragraph + 1, 0};
    } else {
        // Derniere cellule : une ligne vide est ajoutee sous le tableau.
        push_undo(EditKind::Other);
        std::vector<Paragraph> row;
        const std::uint32_t id = paragraphs[caret_.paragraph].table_id;
        const std::uint32_t last_row = paragraphs[end - 1].table_row;
        for (std::uint32_t c = 0; c < cols; ++c) {
            Paragraph cell;
            cell.table_id = id;
            cell.table_row = last_row + 1;
            cell.table_col = c;
            cell.table_cols = cols;
            row.push_back(std::move(cell));
        }
        paragraphs.insert(paragraphs.begin() + static_cast<std::ptrdiff_t>(end),
                          std::make_move_iterator(row.begin()),
                          std::make_move_iterator(row.end()));
        caret_ = Position{end, 0};
        document_.set_dirty(true);
    }
    anchor_ = caret_;
    last_edit_end_ = caret_;
    return true;
}

bool TextEdit::delete_table() {
    if (!caret_in_table()) return false;
    push_undo(EditKind::Other);
    auto& paragraphs = document_.paragraphs();
    const std::size_t begin = table_begin(paragraphs, caret_.paragraph);
    const std::size_t end = table_end(paragraphs, caret_.paragraph);
    paragraphs.erase(paragraphs.begin() + static_cast<std::ptrdiff_t>(begin),
                     paragraphs.begin() + static_cast<std::ptrdiff_t>(end));
    if (paragraphs.empty()) paragraphs.emplace_back();

    const std::size_t at = std::min(begin, paragraphs.size() - 1);
    caret_ = Position{at, 0};
    anchor_ = caret_;
    last_edit_end_ = caret_;
    document_.set_dirty(true);
    return true;
}

// --- Mise en forme ----------------------------------------------------------

CharStyle TextEdit::current_char_style() const {
    if (pending_style_) return *pending_style_;

    if (has_selection()) {
        // Avec une selection, l etat montre par la barre d outils est celui du
        // premier caractere selectionne : c est celui que la bascule lira pour
        // decider d allumer ou d eteindre.
        const Position begin = selection_begin();
        const Paragraph& p = document_.paragraphs()[begin.paragraph];
        return style_at(p.spans, begin.offset);
    }
    const Paragraph& p = document_.paragraphs()[caret_.paragraph];
    return style_before(p.spans, caret_.offset);
}

void TextEdit::mutate_char_style(const std::function<void(CharStyle&)>& mutate,
                                 const std::function<bool(const CharStyle&)>& already_set) {
    if (!has_selection()) {
        // Rien n est modifie : on arme le style de la frappe a venir. Aucune
        // entree d annulation, parce qu il n y a rien a annuler — et le
        // document n est pas sali par un clic qui n a rien ecrit.
        CharStyle style = current_char_style();
        mutate(style);
        pending_style_ = style;
        return;
    }

    const Position begin = selection_begin();
    const Position end = selection_end();
    auto& paragraphs = document_.paragraphs();

    // Bascule : on eteint seulement si TOUTE la selection porte deja
    // l attribut. Sinon on allume — c est ce qui fait qu un premier clic sur
    // une selection a moitie grasse la rend entierement grasse, au lieu de la
    // rendre entierement maigre.
    bool all_set = true;
    bool saw_text = false;
    for (std::size_t i = begin.paragraph; i <= end.paragraph; ++i) {
        const Paragraph& p = paragraphs[i];
        p.ensure_decompressed();
        const std::size_t from = i == begin.paragraph ? begin.offset : 0;
        const std::size_t to = i == end.paragraph ? end.offset : p.text.size();
        if (from >= to) continue; // paragraphe vide ou bord de selection
        saw_text = true;
        if (!style_range_all(p.spans, p.text.size(), from, to, already_set)) {
            all_set = false;
            break;
        }
    }
    // Selection ne couvrant que des coupures de paragraphe : il n y a aucun
    // caractere a habiller, la bascule arme le style de frappe et s arrete.
    if (!saw_text) {
        CharStyle style = current_char_style();
        mutate(style);
        pending_style_ = style;
        return;
    }

    push_undo(EditKind::Other);
    const bool enable = !all_set;
    for (std::size_t i = begin.paragraph; i <= end.paragraph; ++i) {
        Paragraph& p = paragraphs[i];
        p.ensure_decompressed();
        const std::size_t from = i == begin.paragraph ? begin.offset : 0;
        const std::size_t to = i == end.paragraph ? end.offset : p.text.size();
        style_apply(p.spans, p.text.size(), from, to, [&](CharStyle& style) {
            // `mutate` connait l attribut, pas la decision : on la lui donne en
            // reglant l attribut sur `enable` via un aller-retour par le
            // predicat. Concretement, chaque bascule ci-dessous ecrit
            // `style.<attribut> = enable`.
            const bool was = already_set(style);
            if (was != enable) mutate(style);
        });
        ++p.version;
    }
    pending_style_.reset();
    document_.set_dirty(true);
}

void TextEdit::toggle_bold() {
    mutate_char_style([](CharStyle& s) { s.bold = !s.bold; },
                      [](const CharStyle& s) { return s.bold; });
}

void TextEdit::toggle_italic() {
    mutate_char_style([](CharStyle& s) { s.italic = !s.italic; },
                      [](const CharStyle& s) { return s.italic; });
}

void TextEdit::toggle_underline() {
    mutate_char_style([](CharStyle& s) { s.underline = !s.underline; },
                      [](const CharStyle& s) { return s.underline; });
}

void TextEdit::set_font(const std::string& name) {
    if (!has_selection()) {
        // Sans selection, le choix vaut pour la frappe a venir ET devient la
        // police par defaut du document : c est ce qu attend quelqu un qui
        // choisit une police dans un document vide avant d ecrire.
        CharStyle style = current_char_style();
        style.font.clear(); // la plage suit desormais la police par defaut
        pending_style_ = style;
        document_.set_default_font(name);
        document_.set_dirty(true);
        return;
    }

    push_undo(EditKind::Other);
    const Position begin = selection_begin();
    const Position end = selection_end();
    auto& paragraphs = document_.paragraphs();
    for (std::size_t i = begin.paragraph; i <= end.paragraph; ++i) {
        Paragraph& p = paragraphs[i];
        p.ensure_decompressed();
        const std::size_t from = i == begin.paragraph ? begin.offset : 0;
        const std::size_t to = i == end.paragraph ? end.offset : p.text.size();
        style_apply(p.spans, p.text.size(), from, to,
                    [&](CharStyle& style) { style.font = name; });
        ++p.version;
    }
    pending_style_.reset();
    document_.set_dirty(true);
}

// --- Mise en forme de paragraphe --------------------------------------------

void TextEdit::for_each_selected_paragraph(const std::function<void(Paragraph&)>& fn) {
    const Position begin = selection_begin();
    const Position end = selection_end();
    auto& paragraphs = document_.paragraphs();
    for (std::size_t i = begin.paragraph; i <= end.paragraph && i < paragraphs.size(); ++i) {
        fn(paragraphs[i]);
        ++paragraphs[i].version;
    }
}

bool TextEdit::apply_markdown_live() {
    // Une selection en cours veut dire que le curseur n est pas simplement au
    // bout d une frappe : rien a convertir.
    if (has_selection()) return false;

    const Paragraph& probe = document_.paragraphs()[caret_.paragraph];
    const MarkdownRule rule = markdown_detect(probe, caret_.offset);
    if (!rule.applies()) return false;

    // L instantane est pris AVANT le retrait des marqueurs : annuler rend donc
    // le texte tel qu il etait tape, marqueurs compris. C est la seule facon de
    // revoir la syntaxe une fois qu elle a disparu, et c est ce qu attend
    // quelqu un qui vient de taper `**` sans le vouloir.
    push_undo(EditKind::Other);

    Paragraph& p = document_.paragraphs()[caret_.paragraph];
    caret_.offset = markdown_apply(p, rule, caret_.offset);

    if (rule.kind == MarkdownRule::Kind::Paragraph) {
        if (rule.heading != HeadingLevel::None) p.heading = rule.heading;
        if (rule.list != ListKind::None) p.list = rule.list;
    } else {
        // L emphase vient d etre FERMEE : la frappe suivante ne doit pas en
        // heriter. Sans cela, le curseur se retrouve juste apres du texte gras
        // et paragraph_insert() continuerait en gras — l utilisateur aurait
        // tape `**` pour rien.
        CharStyle style = style_before(p.spans, caret_.offset);
        if (rule.bold) style.bold = false;
        if (rule.italic) style.italic = false;
        pending_style_ = style;
    }

    anchor_ = caret_;
    last_edit_end_ = caret_;
    document_.set_dirty(true);
    return true;
}

void TextEdit::set_heading(HeadingLevel level) {
    push_undo(EditKind::Other);
    for_each_selected_paragraph([level](Paragraph& p) {
        if (!p.in_table()) p.heading = level;
    });
    document_.set_dirty(true);
}

void TextEdit::set_list(ListKind kind) {
    push_undo(EditKind::Other);
    for_each_selected_paragraph([kind](Paragraph& p) {
        if (!p.in_table()) p.list = kind;
    });
    document_.set_dirty(true);
}

void TextEdit::set_alignment(Alignment align) {
    push_undo(EditKind::Other);
    for_each_selected_paragraph([align](Paragraph& p) { p.align = align; });
    document_.set_dirty(true);
}

HeadingLevel TextEdit::current_heading() const {
    return document_.paragraphs()[selection_begin().paragraph].heading;
}

ListKind TextEdit::current_list() const {
    return document_.paragraphs()[selection_begin().paragraph].list;
}

Alignment TextEdit::current_alignment() const {
    return document_.paragraphs()[selection_begin().paragraph].align;
}

} // namespace gw::doc
