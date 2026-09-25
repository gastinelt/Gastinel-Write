#include "app/editor_view.h"

#include <algorithm>
#include <cmath>

#include "app/theme.h"
#include "render/draw.h"
#include "render/text_run.h"

namespace gw::app {
namespace {

constexpr int kMarginX = 48;      // marge laterale de la colonne de texte
constexpr int kMarginTop = 22;    // sous la barre de titre
constexpr int kMarginBottom = 44; // au-dessus de la ligne d etat
constexpr int kCaretWidth = 2;
constexpr int kScrollbarWidth = 4;
constexpr int kWheelLinesPerNotch = 3;

// Retrait des paragraphes de liste, en fraction du corps : le marqueur vit
// dans cette gouttiere et le texte commence apres. Exprime relativement au
// corps plutot qu en pixels fixes pour qu un document a 30 px ne se retrouve
// pas avec une puce collee au texte.
constexpr float kListIndentEm = 1.6f;

// Air ajoutee au-dessus d un titre, en fraction de sa propre taille. C est le
// seul espacement vertical du document : le reste des paragraphes est colle,
// comme aujourd hui. Sans elle, un titre qui suit un paragraphe ne se detache
// pas de lui, et la hierarchie ne se lit plus.
constexpr float kHeadingSpaceAbove = 0.45f;

// Largeur ajoutee a la fin d une ligne dont la selection se poursuit.
// Sans elle, une selection multi-lignes s arreterait pile au dernier glyphe et
// on ne verrait pas que la coupure de ligne est prise dedans.
int newline_selection_width(int line_height) { return std::max(4, line_height / 3); }

bool is_break_opportunity(std::uint32_t cp) {
    // Coupure apres une espace ou un tiret. Volontairement pauvre : pas de
    // dictionnaire de cesure, pas de regles typographiques francaises (espace
    // fine avant les deux-points). Le retour a la ligne coupe donc parfois
    // « peut-etre » en deux, ce qui est correct, et jamais un mot au milieu
    // sauf s il est plus large que la colonne.
    return cp == ' ' || cp == '\t' || cp == '-' || cp == 0x2013 || cp == 0x2014;
}

// --- Parcours d une tranche de paragraphe en tenant compte des plages -------
//
// Les trois fonctions ci-dessous sont les equivalents style-conscients de
// render::measure_utf8_range / draw_utf8_range / offset_at_x. Elles ne vivent
// pas dans render/text_run parce qu elles connaissent doc::Paragraph : le
// moteur de rendu ne doit rien savoir du modele de document, sinon la
// separation qui permet de tester chacun a part disparait.
//
// CHEMIN RAPIDE : un paragraphe sans plages (le cas de tout texte non mis en
// forme, donc l immense majorite) resout sa police une seule fois et retombe
// exactement sur le cout d avant l ajout du formatage.

// Style effectif a `offset`, titre compris.
doc::CharStyle style_of(const doc::Paragraph& p, std::size_t offset) {
    return EditorView::effective_style(p, doc::style_at(p.spans, offset));
}

// Ligne de soulignement pour une variante donnee : ordonnee sous la ligne de
// base, et epaisseur. Deduites du corps plutot que fixees, sinon un titre a 32
// px porterait le meme filet d un pixel qu une note de bas de page.
int underline_offset(float pixel_size) { return std::max(1, static_cast<int>(pixel_size * 0.11f)); }
int underline_thickness(float pixel_size) {
    return std::max(1, static_cast<int>(pixel_size * 0.06f));
}

} // namespace

// --- Typographie de la mise en forme ---------------------------------------

float EditorView::heading_scale(doc::HeadingLevel level) {
    // Echelle typographique de la charte : un rapport constant d environ 1,25
    // entre deux niveaux, ce qui donne une hierarchie lisible sans que H1
    // ecrase la page. Les valeurs sont ici et nulle part ailleurs.
    switch (level) {
        case doc::HeadingLevel::H1: return 1.95f;
        case doc::HeadingLevel::H2: return 1.55f;
        case doc::HeadingLevel::H3: return 1.25f;
        case doc::HeadingLevel::None: break;
    }
    return 1.0f;
}

doc::CharStyle EditorView::effective_style(const doc::Paragraph& p, doc::CharStyle style) {
    // Un titre est semi-gras par construction, que ses plages portent le gras
    // ou non : c est le niveau de titre qui fait la graisse, exactement comme
    // dans la charte. Le modele n est pas modifie pour autant — retirer le
    // niveau de titre rend le paragraphe a sa graisse d origine.
    if (p.heading != doc::HeadingLevel::None) style.bold = true;
    return style;
}

namespace {

// Mesure la tranche [begin, end) du paragraphe.
int measure_styled(const doc::Paragraph& p, std::size_t begin, std::size_t end, FontSet& fonts,
                   float pixel_size) {
    p.ensure_decompressed();
    if (p.spans.empty()) {
        const FontFace face =
            fonts.face(std::string(), p.heading != doc::HeadingLevel::None, false, pixel_size);
        return render::measure_utf8_range(p.text, begin, end, *face.font, *face.atlas);
    }

    int width = 0;
    std::size_t i = begin;
    while (i < end && i < p.text.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = render::next_codepoint(p.text, i);
        const doc::CharStyle style = style_of(p, start);
        const FontFace face = fonts.face(style.font, style.bold, style.italic, pixel_size);
        const render::AtlasEntry* entry = face.atlas->get_or_insert(*face.font, cp);
        if (entry != nullptr) width += entry->advance;
    }
    return width;
}

// Dessine la tranche [begin, end) et retourne l abscisse finale. Le parcours
// est le meme que celui de measure_styled : les deux doivent rendre la meme
// avance, sans quoi le curseur derive d un pixel a chaque plage traversee.
int draw_styled(render::Framebuffer& fb, const doc::Paragraph& p, std::size_t begin,
                std::size_t end, int pen_x, int baseline, FontSet& fonts, float pixel_size,
                std::uint32_t argb) {
    p.ensure_decompressed();
    std::size_t i = begin;
    // Le soulignement est un filet continu, pas un filet par glyphe : on
    // accumule la plage soulignee et on la peint d un trait quand elle
    // s interrompt. Peindre glyphe par glyphe laisserait des trous entre les
    // avances, visibles a toute taille.
    int underline_from = -1;

    const auto flush_underline = [&](int pen_to) {
        if (underline_from < 0) return;
        render::fill_rect(fb, underline_from, baseline + underline_offset(pixel_size),
                          pen_to - underline_from, underline_thickness(pixel_size), argb);
        underline_from = -1;
    };

    while (i < end && i < p.text.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = render::next_codepoint(p.text, i);
        const doc::CharStyle style = style_of(p, start);
        const FontFace face = fonts.face(style.font, style.bold, style.italic, pixel_size);
        const render::AtlasEntry* entry = face.atlas->get_or_insert(*face.font, cp);
        if (entry == nullptr) continue; // glyphe absent : saute, pas de .notdef

        if (style.underline) {
            if (underline_from < 0) underline_from = pen_x;
        } else {
            flush_underline(pen_x);
        }

        render::blit_glyph(fb, face.atlas->image(), *entry, pen_x, baseline, argb);
        pen_x += entry->advance;
    }
    flush_underline(pen_x);
    return pen_x;
}

// Frontiere de caractere de [begin, end) dont le bord gauche est le plus
// proche de `x`, compte depuis le debut de la tranche. Meme regle de bascule a
// mi-caractere que render::offset_at_x.
std::size_t offset_at_x_styled(const doc::Paragraph& p, std::size_t begin, std::size_t end, int x,
                               FontSet& fonts, float pixel_size) {
    p.ensure_decompressed();
    if (x <= 0) return begin;

    int pen = 0;
    std::size_t i = begin;
    while (i < end && i < p.text.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = render::next_codepoint(p.text, i);
        const doc::CharStyle style = style_of(p, start);
        const FontFace face = fonts.face(style.font, style.bold, style.italic, pixel_size);
        const render::AtlasEntry* entry = face.atlas->get_or_insert(*face.font, cp);
        const int advance = entry != nullptr ? entry->advance : 0;
        if (x < pen + advance / 2) return start;
        pen += advance;
    }
    return end < p.text.size() ? end : p.text.size();
}

// Police de reference d un paragraphe : celle qui fixe sa hauteur de ligne.
// C est la police par defaut a la taille du paragraphe, et non celle de sa
// premiere plage — sans quoi un seul mot dans une police a grandes hampes
// changerait l interligne de tout le paragraphe.
FontFace metrics_face(const doc::Paragraph& p, FontSet& fonts, float pixel_size) {
    return fonts.face(std::string(), p.heading != doc::HeadingLevel::None, false, pixel_size);
}

} // namespace

void EditorView::layout(const doc::TextEdit& edit, int width, int height, int top_offset,
                        int inset, FontSet& fonts, float body_size) {
    const FontFace body = fonts.face(std::string(), false, false, body_size);

    line_height_ = std::max(1, render::line_height(*body.font, body_size));
    ascent_ = render::ascent_pixels(*body.font, body_size);

    content_x_ = kMarginX + inset;
    content_y_ = top_offset + kMarginTop;
    content_width_ = std::max(1, width - 2 * (kMarginX + inset) - kScrollbarWidth * 2);
    content_height_ = std::max(0, height - content_y_ - kMarginBottom - inset);
    visible_lines_ = std::max(1, content_height_ / line_height_);

    lines_.clear();
    const auto& paragraphs = edit.document().paragraphs();
    paragraph_first_line_.assign(paragraphs.size(), 0);
    if (paragraph_cache_.size() != paragraphs.size()) {
        paragraph_cache_.resize(paragraphs.size());
    }

    // Compteur de liste numerotee : il repart a 1 des qu un paragraphe qui
    // n est pas une liste numerotee s intercale. C est ce qui fait que deux
    // listes separees par un titre recommencent chacune a 1, sans avoir a
    // stocker un numero dans le modele.
    int number = 0;

    for (std::size_t p = 0; p < paragraphs.size(); ++p) {
        paragraph_first_line_[p] = lines_.size();
        const doc::Paragraph& paragraph = paragraphs[p];
        const float size = body_size * heading_scale(paragraph.heading);

        const FontFace metrics = metrics_face(paragraph, fonts, size);
        const int height_of_line = std::max(1, render::line_height(*metrics.font, size));
        const int ascent_of_line = render::ascent_pixels(*metrics.font, size);

        const int indent = paragraph.list == doc::ListKind::None
                                ? 0
                                : static_cast<int>(body_size * kListIndentEm);

        std::string marker;
        if (paragraph.list == doc::ListKind::Bullet) {
            number = 0;
            marker = "\xE2\x80\xA2"; // U+2022 PUCE
        } else if (paragraph.list == doc::ListKind::Numbered) {
            marker = std::to_string(++number) + ".";
        } else {
            number = 0;
        }

        // Air au-dessus d un titre : portee par la ligne elle-meme plutot que
        // par une marge separee, pour que le cumul des hauteurs reste la seule
        // facon de placer une ligne.
        const int space_above =
            paragraph.heading == doc::HeadingLevel::None || p == 0
                ? 0
                : static_cast<int>(size * kHeadingSpaceAbove);

        const int wrap_width = std::max(1, content_width_ - indent);

        const bool cache_hit = (p < paragraph_cache_.size()) &&
                               (paragraph_cache_[p].version == paragraph.version) &&
                               (paragraph_cache_[p].wrap_width == wrap_width) &&
                               (paragraph_cache_[p].body_size == body_size) &&
                               (paragraph_cache_[p].space_above == space_above);

        if (cache_hit) {
            for (std::size_t li = 0; li < paragraph_cache_[p].lines.size(); ++li) {
                VisualLine line = paragraph_cache_[p].lines[li];
                line.paragraph = p;
                if (li == 0) line.marker = marker;
                lines_.push_back(std::move(line));
            }
            continue;
        }

        paragraph.ensure_decompressed();
        std::vector<VisualLine> p_lines;
        const auto push_line = [&](std::size_t begin, std::size_t end, bool continues) {
            VisualLine line;
            line.paragraph = p;
            line.begin = begin;
            line.end = end;
            line.continues = continues;
            line.height = height_of_line;
            line.ascent = ascent_of_line;
            line.indent = indent;
            if (p_lines.empty()) {
                line.height += space_above;
                line.ascent += space_above;
                line.space_above = space_above;
            }
            p_lines.push_back(std::move(line));
        };

        if (paragraph.text.empty()) {
            // Un paragraphe vide occupe quand meme une ligne : c est une ligne
            // blanche a l ecran, et le curseur doit pouvoir s y poser.
            push_line(0, 0, false);
        } else {
            std::size_t line_begin = 0;
            std::size_t last_break = std::string::npos; // premier octet apres la coupure
            int width_so_far = 0;
            std::size_t i = 0;

            while (i < paragraph.text.size()) {
                const std::size_t glyph_begin = i;
                const std::uint32_t cp = render::next_codepoint(paragraph.text, i);
                const doc::CharStyle style = style_of(paragraph, glyph_begin);
                const FontFace face = fonts.face(style.font, style.bold, style.italic, size);
                const render::AtlasEntry* entry = face.atlas->get_or_insert(*face.font, cp);
                const int advance = entry != nullptr ? entry->advance : 0;

                if (width_so_far + advance > wrap_width && glyph_begin > line_begin) {
                    // Coupure au dernier point de coupure vu, sinon en plein
                    // mot : un mot plus large que la colonne doit quand meme
                    // s afficher, faute de quoi la boucle n avancerait pas.
                    const bool at_word = last_break != std::string::npos && last_break > line_begin;
                    const std::size_t cut = at_word ? last_break : glyph_begin;
                    push_line(line_begin, cut, line_begin != 0);
                    line_begin = cut;
                    last_break = std::string::npos;
                    width_so_far = 0;
                    i = cut; // on reprend a la coupure : les glyphes sautes seront remesures
                    continue;
                }

                width_so_far += advance;
                if (is_break_opportunity(cp)) last_break = i;
            }
            push_line(line_begin, paragraph.text.size(), line_begin != 0);
        }

        // Alignement : il ne se calcule qu une fois les coupures connues, et
        // uniquement pour les paragraphes qui ne sont pas a gauche — mesurer
        // chaque ligne une seconde fois n a pas de raison d etre payee par le
        // cas courant.
        if (paragraph.align != doc::Alignment::Left) {
            for (VisualLine& line : p_lines) {
                const int line_width =
                    measure_styled(paragraph, line.begin, line.end, fonts, size);
                const int free_space = std::max(0, wrap_width - line_width);
                line.offset_x =
                    paragraph.align == doc::Alignment::Center ? free_space / 2 : free_space;
            }
        }

        // Sauvegarde dans le cache
        paragraph_cache_[p].version = paragraph.version;
        paragraph_cache_[p].wrap_width = wrap_width;
        paragraph_cache_[p].body_size = body_size;
        paragraph_cache_[p].space_above = space_above;
        paragraph_cache_[p].lines = p_lines;

        for (std::size_t li = 0; li < p_lines.size(); ++li) {
            VisualLine& line = p_lines[li];
            if (li == 0) line.marker = marker;
            lines_.push_back(std::move(line));
        }
    }

    // Le document a pu raccourcir sous la fenetre de defilement.
    if (first_line_ >= lines_.size()) {
        first_line_ = lines_.empty() ? 0 : lines_.size() - 1;
    }
}

// --- Correspondance position <-> ligne -------------------------------------

std::size_t EditorView::line_index_for(doc::Position position) const {
    if (lines_.empty()) return 0;
    const std::size_t start = (position.paragraph < paragraph_first_line_.size())
                                  ? paragraph_first_line_[position.paragraph]
                                  : 0;
    for (std::size_t i = start; i < lines_.size(); ++i) {
        const VisualLine& line = lines_[i];
        if (line.paragraph != position.paragraph) {
            if (line.paragraph > position.paragraph) break;
            continue;
        }
        if (position.offset < line.end) return i;
        // Fin de tranche : la position appartient a cette ligne seulement si
        // c est la derniere du paragraphe. Sinon elle est le debut de la
        // suivante — c est ce qui fait apparaitre le curseur en tete de la
        // ligne suivante quand on ecrit jusqu au bord de la colonne.
        const bool last_of_paragraph =
            i + 1 >= lines_.size() || lines_[i + 1].paragraph != line.paragraph;
        if (position.offset == line.end && last_of_paragraph) return i;
    }
    return start < lines_.size() ? start : lines_.size() - 1;
}

int EditorView::line_top(std::size_t index) const {
    if (index < first_line_) return content_y_;
    int y = content_y_;
    for (std::size_t i = first_line_; i < index && i < lines_.size(); ++i) y += lines_[i].height;
    return y;
}

int EditorView::caret_x_for(const doc::TextEdit& edit, doc::Position position, FontSet& fonts,
                            float body_size) const {
    if (lines_.empty()) return content_x_;
    const VisualLine& line = lines_[line_index_for(position)];
    const doc::Paragraph& paragraph = edit.document().paragraphs()[line.paragraph];
    const float size = body_size * heading_scale(paragraph.heading);
    const std::size_t offset = std::clamp(position.offset, line.begin, line.end);
    return content_x_ + line.indent + line.offset_x +
           measure_styled(paragraph, line.begin, offset, fonts, size);
}

doc::Position EditorView::position_on_line(const doc::TextEdit& edit, std::size_t line_index,
                                           int x, FontSet& fonts, float body_size) const {
    if (lines_.empty()) return doc::Position{};
    const VisualLine& line = lines_[std::min(line_index, lines_.size() - 1)];
    const doc::Paragraph& paragraph = edit.document().paragraphs()[line.paragraph];
    const float size = body_size * heading_scale(paragraph.heading);
    const std::size_t offset =
        offset_at_x_styled(paragraph, line.begin, line.end,
                           x - content_x_ - line.indent - line.offset_x, fonts, size);
    return doc::Position{line.paragraph, offset};
}

bool EditorView::contains_point(int x, int y) const {
    return y >= content_y_ && y < content_y_ + content_height_ && x >= 0;
}

doc::Position EditorView::position_at_point(const doc::TextEdit& edit, int x, int y,
                                            FontSet& fonts, float body_size) const {
    if (lines_.empty()) return doc::Position{};

    // Un clic au-dessus ou au-dessous du texte ne doit pas etre ignore : il
    // vise la premiere ou la derniere ligne visible. C est ce qui rend le
    // glisser-selectionner utilisable jusqu au bord.
    std::size_t index = first_line_;
    if (y > content_y_) {
        int top = content_y_;
        for (std::size_t i = first_line_; i < lines_.size(); ++i) {
            index = i;
            if (y < top + lines_[i].height) break;
            top += lines_[i].height;
        }
    }
    return position_on_line(edit, index, x, fonts, body_size);
}

// --- Deplacements verticaux -------------------------------------------------

int EditorView::resolve_desired_x(const doc::TextEdit& edit, FontSet& fonts,
                                  float body_size) const {
    if (desired_x_ < 0) desired_x_ = caret_x_for(edit, edit.caret(), fonts, body_size);
    return desired_x_;
}

doc::Position EditorView::position_above(const doc::TextEdit& edit, FontSet& fonts,
                                         float body_size) const {
    const int x = resolve_desired_x(edit, fonts, body_size);
    const std::size_t line = line_index_for(edit.caret());
    if (line == 0) return doc::Position{0, 0}; // deja en haut : on va au tout debut
    return position_on_line(edit, line - 1, x, fonts, body_size);
}

doc::Position EditorView::position_below(const doc::TextEdit& edit, FontSet& fonts,
                                         float body_size) const {
    const int x = resolve_desired_x(edit, fonts, body_size);
    const std::size_t line = line_index_for(edit.caret());
    if (line + 1 >= lines_.size()) {
        const std::size_t last = edit.document().paragraphs().size() - 1;
        return doc::Position{last, edit.document().paragraphs()[last].text.size()};
    }
    return position_on_line(edit, line + 1, x, fonts, body_size);
}

doc::Position EditorView::position_page(const doc::TextEdit& edit, FontSet& fonts,
                                        float body_size, int direction) const {
    const int x = resolve_desired_x(edit, fonts, body_size);
    const auto line = static_cast<std::ptrdiff_t>(line_index_for(edit.caret()));
    const auto jump = static_cast<std::ptrdiff_t>(visible_lines_) * (direction < 0 ? -1 : 1);
    const auto target =
        std::clamp<std::ptrdiff_t>(line + jump, 0, static_cast<std::ptrdiff_t>(lines_.size()) - 1);
    return position_on_line(edit, static_cast<std::size_t>(target), x, fonts, body_size);
}

doc::Position EditorView::line_start(const doc::TextEdit& edit) const {
    if (lines_.empty()) return doc::Position{};
    const VisualLine& line = lines_[line_index_for(edit.caret())];
    return doc::Position{line.paragraph, line.begin};
}

doc::Position EditorView::line_end(const doc::TextEdit& edit) const {
    if (lines_.empty()) return doc::Position{};
    const VisualLine& line = lines_[line_index_for(edit.caret())];
    return doc::Position{line.paragraph, line.end};
}

// --- Defilement -------------------------------------------------------------

bool EditorView::scroll_by(int delta) {
    if (lines_.size() <= static_cast<std::size_t>(visible_lines_)) return false;

    const int notches = delta / 120;
    if (notches == 0) return false;
    const auto max_first =
        static_cast<std::ptrdiff_t>(lines_.size()) - static_cast<std::ptrdiff_t>(visible_lines_);
    const auto wanted = static_cast<std::ptrdiff_t>(first_line_) -
                        static_cast<std::ptrdiff_t>(notches) * kWheelLinesPerNotch;
    const auto clamped = std::clamp<std::ptrdiff_t>(wanted, 0, std::max<std::ptrdiff_t>(0, max_first));

    if (static_cast<std::size_t>(clamped) == first_line_) return false;
    first_line_ = static_cast<std::size_t>(clamped);
    reset_caret_animation();
    return true;
}

void EditorView::ensure_caret_visible(const doc::TextEdit& edit) {
    if (lines_.empty()) return;
    const std::size_t line = line_index_for(edit.caret());
    if (line < first_line_) {
        first_line_ = line;
        return;
    }
    // Vers le bas, le test se fait en PIXELS et non en nombre de lignes : avec
    // des lignes de hauteurs differentes, on s'assure que la ligne du curseur
    // tienne dans la zone.
    // Au lieu d'une double boucle quadratique O(M^2), on cumule en marche arriere
    // depuis `line` vers le haut : au plus ~35 iterations au lieu de milliards.
    int used = 0;
    std::size_t new_first = line;
    for (std::size_t i = line; ; --i) {
        used += lines_[i].height;
        if (used > content_height_) {
            new_first = (i == line) ? line : (i + 1);
            break;
        }
        if (i == 0) {
            new_first = 0;
            break;
        }
    }
    if (new_first > first_line_) {
        first_line_ = new_first;
    }
}

// --- Animation fluide du curseur (façon Word) -------------------------------

void EditorView::reset_caret_animation() {
    anim_caret_x_ = -1.0f;
    anim_caret_y_ = -1.0f;
    anim_caret_h_ = 0.0f;
    is_animating_ = false;
}

void EditorView::update_caret_target(const doc::TextEdit& edit, FontSet& fonts, float body_size,
                                     bool smooth_enabled) {
    if (lines_.empty()) {
        reset_caret_animation();
        return;
    }

    const std::size_t line = line_index_for(edit.caret());
    if (line >= lines_.size()) {
        reset_caret_animation();
        return;
    }

    const int target_x = caret_x_for(edit, edit.caret(), fonts, body_size);
    const int top = line_top(line);
    const VisualLine& target = lines_[line];
    const int target_y = top + target.space_above;
    const int target_h = std::max(1, target.height - target.space_above);

    if (!smooth_enabled || anim_caret_x_ < 0.0f) {
        anim_caret_x_ = static_cast<float>(target_x);
        anim_caret_y_ = static_cast<float>(target_y);
        anim_caret_h_ = static_cast<float>(target_h);
        target_caret_x_ = anim_caret_x_;
        target_caret_y_ = anim_caret_y_;
        target_caret_h_ = anim_caret_h_;
        is_animating_ = false;
        return;
    }

    const float dy = std::abs(static_cast<float>(target_y) - anim_caret_y_);
    const float dx = std::abs(static_cast<float>(target_x) - anim_caret_x_);
    if (dy > static_cast<float>(line_height_ * 2.2f) || dx > 500.0f) {
        anim_caret_x_ = static_cast<float>(target_x);
        anim_caret_y_ = static_cast<float>(target_y);
        anim_caret_h_ = static_cast<float>(target_h);
        target_caret_x_ = anim_caret_x_;
        target_caret_y_ = anim_caret_y_;
        target_caret_h_ = anim_caret_h_;
        is_animating_ = false;
        return;
    }

    if (std::abs(static_cast<float>(target_x) - anim_caret_x_) < 0.5f &&
        std::abs(static_cast<float>(target_y) - anim_caret_y_) < 0.5f) {
        anim_caret_x_ = static_cast<float>(target_x);
        anim_caret_y_ = static_cast<float>(target_y);
        anim_caret_h_ = static_cast<float>(target_h);
        target_caret_x_ = anim_caret_x_;
        target_caret_y_ = anim_caret_y_;
        target_caret_h_ = anim_caret_h_;
        is_animating_ = false;
        return;
    }

    target_caret_x_ = static_cast<float>(target_x);
    target_caret_y_ = static_cast<float>(target_y);
    target_caret_h_ = static_cast<float>(target_h);
    is_animating_ = true;
}

bool EditorView::step_animation(float dt) {
    if (!is_animating_) return false;

    // Glissement exponentiel fluide Word (~70-80ms de convergence)
    const float decay = std::exp(-30.0f * dt);
    anim_caret_x_ = target_caret_x_ + (anim_caret_x_ - target_caret_x_) * decay;
    anim_caret_y_ = target_caret_y_ + (anim_caret_y_ - target_caret_y_) * decay;
    anim_caret_h_ = target_caret_h_ + (anim_caret_h_ - target_caret_h_) * decay;

    if (std::abs(target_caret_x_ - anim_caret_x_) < 0.5f &&
        std::abs(target_caret_y_ - anim_caret_y_) < 0.5f) {
        anim_caret_x_ = target_caret_x_;
        anim_caret_y_ = target_caret_y_;
        anim_caret_h_ = target_caret_h_;
        is_animating_ = false;
        return false;
    }

    return true;
}

// --- Dessin -----------------------------------------------------------------

void EditorView::draw(render::Framebuffer& fb, const doc::TextEdit& edit, FontSet& fonts,
                      float body_size, bool caret_visible) const {
    if (lines_.empty()) return;

    const Theme& palette = theme();
    const auto& paragraphs = edit.document().paragraphs();

    const doc::Position sel_begin = edit.selection_begin();
    const doc::Position sel_end = edit.selection_end();
    const bool has_selection = edit.has_selection();

    // Derniere ligne effectivement affichee, determinee par la place restante
    // et non par un compte de lignes.
    std::size_t last = first_line_;
    {
        int used = 0;
        while (last < lines_.size() && used + lines_[last].height <= content_height_) {
            used += lines_[last].height;
            ++last;
        }
        // Toujours au moins une ligne, meme si elle deborde : sinon un titre
        // plus haut que la zone ne s afficherait jamais.
        if (last == first_line_ && first_line_ < lines_.size()) last = first_line_ + 1;
    }

    int y = content_y_;
    for (std::size_t i = first_line_; i < last; ++i) {
        const VisualLine& line = lines_[i];
        const doc::Paragraph& paragraph = paragraphs[line.paragraph];
        const float size = body_size * heading_scale(paragraph.heading);
        const int text_x = content_x_ + line.indent + line.offset_x;
        const int baseline = y + line.ascent;

        // Marqueur de liste, pose dans la gouttiere du retrait. Il suit
        // l alignement du paragraphe pour rester colle a son texte.
        if (!line.marker.empty()) {
            const FontFace marker_face =
                fonts.face(std::string(), paragraph.heading != doc::HeadingLevel::None, false,
                           size);
            const int marker_width =
                render::measure_utf8(line.marker, *marker_face.font, *marker_face.atlas);
            // Aligne a DROITE dans la gouttiere : « 9. » et « 10. » alignent
            // alors leur point, ce qui est la seule facon d obtenir une colonne
            // de numeros droite sans mesurer toute la liste d avance.
            const int marker_x = std::max(content_x_ + line.offset_x,
                                          text_x - marker_width - std::max(4, line.indent / 5));
            render::draw_utf8(fb, line.marker, marker_x, baseline, *marker_face.font,
                              *marker_face.atlas, palette.foreground);
        }

        if (line.begin != line.end) {
            draw_styled(fb, paragraph, line.begin, line.end, text_x, baseline, fonts, size,
                        palette.foreground);
        }

        if (has_selection) {
            // Intersection de la selection avec cette ligne, en octets.
            const doc::Position line_from{line.paragraph, line.begin};
            const doc::Position line_to{line.paragraph, line.end};
            if (!(line_to < sel_begin || sel_end < line_from)) {
                const std::size_t from = sel_begin.paragraph == line.paragraph
                                             ? std::max(sel_begin.offset, line.begin)
                                             : line.begin;
                const std::size_t to = sel_end.paragraph == line.paragraph
                                           ? std::min(sel_end.offset, line.end)
                                           : line.end;
                if (from <= to) {
                    const int x0 =
                        text_x + measure_styled(paragraph, line.begin, from, fonts, size);
                    const int x1 =
                        text_x + measure_styled(paragraph, line.begin, to, fonts, size);

                    // La coupure de *paragraphe* fait partie de la selection
                    // quand celle-ci se poursuit au-dela. Une coupure de ligne
                    // automatique, elle, n est pas un caractere : l
                    // elargissement ne s applique donc qu a la derniere ligne
                    // d un paragraphe.
                    const bool last_of_paragraph =
                        i + 1 >= lines_.size() || lines_[i + 1].paragraph != line.paragraph;
                    const int extra = (last_of_paragraph && line_to < sel_end)
                                          ? newline_selection_width(line.height)
                                          : 0;
                    // Selection vide sur cette ligne (elle s arrete pile a son
                    // debut) : rien a peindre. Sans ce test, un filet d un
                    // pixel resterait accroche en tete de la ligne suivante.
                    const int band = (x1 - x0) + extra;
                    if (band > 0) {
                        // La bande couvre la ligne de texte, mais PAS l air
                        // ajoutee au-dessus d un titre : la peindre ferait
                        // deborder la selection dans le paragraphe precedent.
                        render::fill_rect(fb, x0, y + line.space_above, band,
                                          line.height - line.space_above, palette.selection_fill);
                        if (from != to) {
                            draw_styled(fb, paragraph, from, to, x0, baseline, fonts, size,
                                        palette.selection_text);
                        }
                    }
                }
            }
        }

        y += line.height;
    }

    // --- Curseur ---
    if (caret_visible || is_animating_) {
        const std::size_t line = line_index_for(edit.caret());
        if (line >= first_line_ && line < last) {
            const bool on_band = edit.has_selection() && edit.caret() == sel_begin;
            const VisualLine& target = lines_[line];
            int x = 0;
            int top = 0;
            int h = 0;
            if (anim_caret_x_ >= 0.0f) {
                x = static_cast<int>(std::round(anim_caret_x_));
                top = static_cast<int>(std::round(anim_caret_y_));
                h = static_cast<int>(std::round(anim_caret_h_));
            } else {
                x = caret_x_for(edit, edit.caret(), fonts, body_size);
                top = line_top(line) + target.space_above;
                h = std::max(1, target.height - target.space_above);
            }

            render::fill_rect(fb, x, top, kCaretWidth, std::max(1, h),
                              on_band ? palette.selection_text : palette.caret);
        }
    }

    // --- Rail de defilement ---
    //
    // Indicateur, pas controle : il ne se saisit pas a la souris. Il repond a
    // la seule question qu on se pose vraiment sans lui — « ou suis-je dans le
    // document ». Le rendre saisissable demanderait un etat de glissement de
    // plus, pour un geste que la molette couvre deja.
    if (lines_.size() > static_cast<std::size_t>(visible_lines_)) {
        const int track_x = fb.width - content_x_ + kMarginX / 2;
        const int track_h = content_height_;
        const double ratio = static_cast<double>(visible_lines_) / static_cast<double>(lines_.size());
        const int thumb_h = std::max(line_height_, static_cast<int>(track_h * ratio));
        const double progress = static_cast<double>(first_line_) /
                                static_cast<double>(lines_.size() - static_cast<std::size_t>(visible_lines_));
        const int thumb_y = content_y_ + static_cast<int>((track_h - thumb_h) * progress);
        render::fill_rect(fb, track_x, thumb_y, kScrollbarWidth, thumb_h, palette.scrollbar);
    }

    // Compression des paragraphes distants hors de l'ecran pour documents massifs.
    // Laisse une marge de securite de 10 paragraphes au-dessus et en-dessous de l'ecran.
    // Ne compresse jamais le paragraphe actif du curseur.
    if (paragraphs.size() > 20) {
        const std::size_t min_visible_p = (first_line_ < lines_.size()) ? lines_[first_line_].paragraph : 0;
        const std::size_t max_visible_p = (last > 0 && last - 1 < lines_.size()) ? lines_[last - 1].paragraph : min_visible_p;
        const std::size_t caret_p = edit.caret().paragraph;
        constexpr std::size_t kMargin = 10;
        const std::size_t safe_min = min_visible_p > kMargin ? min_visible_p - kMargin : 0;
        const std::size_t safe_max = max_visible_p + kMargin;

        for (std::size_t p = 0; p < paragraphs.size(); ++p) {
            if (p >= safe_min && p <= safe_max) continue;
            if (p == caret_p) continue;
            if (!paragraphs[p].is_compressed() && paragraphs[p].text.size() >= 64) {
                paragraphs[p].compress();
            }
        }
    }
}

} // namespace gw::app
