// tools/screen_render.cpp — rendu hors fenetre des ecrans, en BMP.
//
// POURQUOI CETTE CIBLE : tout le rendu du programme est CPU et aboutit dans un
// `Framebuffer`. Les backends OpenGL et GDI ne font que l afficher. On peut
// donc composer un ecran complet, exactement comme le fait l application, sans
// creer la moindre fenetre — et regarder le resultat.
//
// C est ce qui rend une modification d interface verifiable autrement qu en
// cliquant : le mode sombre, par exemple, se controle en comparant deux images
// produites ici, pas en decrivant ce qu il devrait faire.
//
// Le BMP est choisi parce qu il s ecrit en vingt lignes sans dependance et que
// le framebuffer est deja en BGRA — l ecriture est une copie de lignes. Le
// projet n a pas d encodeur PNG (seulement un decodeur), et en ajouter un pour
// un outil de developpement serait disproportionne.
//
// Usage : gw_screen_render <repertoire_assets> <repertoire_de_sortie>

#include "app/chrome.h"
#include "app/editor_view.h"
#include "app/fonts.h"
#include "app/format_bar.h"
#include "app/context_menu.h"
#include "app/menu_bar.h"
#include "doc/markdown_live.h"
#include "app/home_screen.h"
#include "app/settings.h"
#include "app/settings_screen.h"
#include "app/theme.h"
#include "doc/text_edit.h"
#include "render/bitmap.h"
#include "render/draw.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 1000;
constexpr int kHeight = 700;

bool write_bmp(const std::string& path, const gw::render::Framebuffer& fb) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const int row_bytes = fb.width * 4;
    const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(row_bytes) *
                                      static_cast<std::uint32_t>(fb.height);
    const std::uint32_t offset = 14 + 40;

    std::vector<std::uint8_t> header(offset, 0);
    const auto put16 = [&](std::size_t at, std::uint32_t v) {
        header[at] = static_cast<std::uint8_t>(v & 0xFF);
        header[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    };
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        put16(at, v & 0xFFFF);
        put16(at + 2, (v >> 16) & 0xFFFF);
    };

    header[0] = 'B';
    header[1] = 'M';
    put32(2, offset + pixel_bytes);
    put32(10, offset);
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(fb.width));
    // Hauteur negative : lignes stockees de haut en bas, comme le framebuffer.
    put32(22, static_cast<std::uint32_t>(-fb.height));
    put16(26, 1);
    put16(28, 32);
    put32(34, pixel_bytes);

    std::fwrite(header.data(), 1, header.size(), f);
    for (int y = 0; y < fb.height; ++y) {
        std::fwrite(fb.pixels.data() + static_cast<std::size_t>(y) * fb.width, 4,
                    static_cast<std::size_t>(fb.width), f);
    }
    std::fclose(f);
    return true;
}

// Reproduit l ordre de composition de App::render_frame : fond, contenu,
// chrome par-dessus, filet de bordure. Le respecter est ce qui fait que
// l image produite ici est celle que l application afficherait.
void compose(gw::render::Framebuffer& fb, gw::app::FontSet& fonts, gw::app::TitleBar& bar,
             const std::string& title, void (*content)(gw::render::Framebuffer&,
                                                       gw::app::FontSet&)) {
    fb.clear(gw::app::theme().background);
    content(fb, fonts);
    bar.draw(fb, title, fonts.font(gw::app::Weight::Regular),
             fonts.atlas(gw::app::Weight::Regular, 14.0f), false);
    gw::render::stroke_rect(fb, 0, 0, fb.width, fb.height, 1, gw::app::theme().foreground);
}

gw::app::HomeScreen g_home;
gw::app::EditorView g_editor_view;
gw::doc::TextEdit g_editor;
gw::app::SettingsScreen g_settings_screen;
gw::app::FormatBar g_format_bar;
gw::app::MenuBar g_menu_bar;
gw::app::ContextMenu g_dropdown;

// Document du mode clavier-first, construit par une frappe simulee.
gw::doc::TextEdit g_keyboard;
gw::app::EditorView g_keyboard_view;
gw::app::EditorView g_rich_view;
gw::doc::TextEdit g_rich;
gw::app::FormatState g_format_state;
gw::app::Settings g_settings;
std::vector<std::string> g_recent;

void draw_home(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_home.draw(fb, fonts, g_recent);
}

void draw_settings(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_settings_screen.draw(fb, fonts, g_settings);
}

// L editeur est le seul ecran dont l apparence depend d un etat interne
// (curseur, selection, defilement) plutot que d une simple mise en page. Le
// rendre ici est donc la seule facon de verifier le retour a la ligne et la
// selection multi-lignes autrement qu en cliquant dans l application.
void draw_editor(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_editor_view.draw(fb, g_editor, fonts, 16.0f, true);
}

// Ecran de mise en forme : le meme editeur, mais avec la barre d outils et un
// document qui exerce chaque decision typographique une par une. C est la seule
// verification possible de ce chantier autrement qu en cliquant — un test
// unitaire peut prouver que le modele porte « ce mot est en gras », pas que le
// mot sort effectivement plus gras a l ecran.
void draw_rich(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_rich_view.draw(fb, g_rich, fonts, 16.0f, true);
    g_menu_bar.draw(fb, fonts);
    g_format_bar.draw(fb, fonts, g_format_state);
}

// Ecran clavier-first : le meme editeur, SANS la barre d outils, sur un
// document qu aucune barre n a touche — tout y a ete produit par la frappe de
// syntaxe Markdown. Mis cote a cote avec mise_en_forme*.bmp, c est la preuve
// visuelle que les deux modes de saisie aboutissent au meme rendu, la ou un
// test unitaire ne prouve que l egalite des modeles.
void draw_keyboard(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_keyboard_view.draw(fb, g_keyboard, fonts, 16.0f, true);
    // La barre de menu reste, elle : c est le seul element de chrome que le
    // mode clavier-first conserve, et l image doit montrer a quel point elle
    // est discrete au repos.
    g_menu_bar.draw(fb, fonts);
}

// Ecran « menu ouvert » : le meme editeur, avec le deroulant du menu Edition
// deploye. Il verifie ce qu aucun test unitaire ne peut voir — que le menu
// s ancre bien sous son titre, qu il passe par-dessus la barre de mise en forme
// et le texte, et que les entrees grisees se distinguent des actives.
//
// LES ENTREES CI-DESSOUS SONT UN ECHANTILLON DE PIXELS, PAS LA LISTE DE
// REFERENCE. La liste qui fait foi est construite par App::open_main_menu() et
// nulle part ailleurs ; celle-ci n existe que pour donner quelque chose a
// dessiner, et personne ne doit la lire pour savoir ce que contient le menu.
void draw_menu_open(gw::render::Framebuffer& fb, gw::app::FontSet& fonts) {
    g_rich_view.draw(fb, g_rich, fonts, 16.0f, true);
    g_menu_bar.draw(fb, fonts);
    g_format_bar.draw(fb, fonts, g_format_state);
    g_dropdown.draw(fb, fonts.font(gw::app::Weight::Regular),
                    fonts.atlas(gw::app::Weight::Regular, 14.0f));
}

// Applique un style de caractere a une plage d octets d un paragraphe.
gw::doc::Paragraph rich_paragraph(const std::string& text, std::size_t begin, std::size_t end,
                                  bool bold, bool italic, bool underline) {
    gw::doc::Paragraph p{text};
    gw::doc::style_apply(p.spans, p.text.size(), begin, end, [&](gw::doc::CharStyle& s) {
        s.bold = bold;
        s.italic = italic;
        s.underline = underline;
    });
    return p;
}

// Frappe caractere par caractere, conversion tentee apres chacun : exactement
// la sequence de App::on_char en mode clavier-first.
void type_markdown(const std::string& text) {
    for (char c : text) {
        if (c == '\n') {
            g_keyboard.insert_paragraph_break();
            continue;
        }
        g_keyboard.insert_text(std::string(1, c));
        g_keyboard.apply_markdown_live();
    }
}

void seed_keyboard() {
    // Le corps de texte EN PREMIER, volontairement : un paragraphe herite des
    // attributs de celui qu on vient de couper (cf. paragraph_split), donc le
    // placer sous un titre le rendrait titre lui aussi, et le gras y serait
    // invisible. C est une limite connue du modele, pas du mode clavier —
    // l ordre choisi ici la contourne pour que l image montre le gras.
    type_markdown("Un paragraphe ou le **gras** et l *italique* sont arrives "
                  "par la syntaxe, sans jamais toucher la barre d outils.\n");
    type_markdown("# Titre de niveau 1\n");
    // Le paragraphe herite du H1 : taper `## ` doit malgre tout donner un H2.
    type_markdown("## Titre de niveau 2\n");
    type_markdown("### Titre de niveau 3\n");
    type_markdown("- premiere puce\n");
    type_markdown("deuxieme puce, heritee du retour a la ligne\n");
    type_markdown("1. bascule en liste numerotee\n");
    type_markdown("Une _autre_ forme d italique, et un nom_de_variable "
                  "qui doit rester intact.");
}

void seed_rich() {
    gw::doc::Document doc;
    auto& ps = doc.paragraphs();

    gw::doc::Paragraph h1{"Titre de niveau 1"};
    h1.heading = gw::doc::HeadingLevel::H1;
    ps.push_back(h1);

    ps.push_back(rich_paragraph(
        "Un paragraphe de corps ou le gras, l italique et le souligne coexistent "
        "sur des plages differentes, y compris quand ils se chevauchent.",
        27, 32, true, false, false));
    gw::doc::style_apply(ps.back().spans, ps.back().text.size(), 35, 44,
                         [](gw::doc::CharStyle& s) { s.italic = true; });
    gw::doc::style_apply(ps.back().spans, ps.back().text.size(), 48, 56,
                         [](gw::doc::CharStyle& s) { s.underline = true; });
    // Chevauchement : les trois attributs sur la meme plage.
    gw::doc::style_apply(ps.back().spans, ps.back().text.size(), 100, 112,
                         [](gw::doc::CharStyle& s) {
                             s.bold = true;
                             s.italic = true;
                             s.underline = true;
                         });

    gw::doc::Paragraph h2{"Titre de niveau 2"};
    h2.heading = gw::doc::HeadingLevel::H2;
    ps.push_back(h2);

    for (const char* item : {"Premier point de la liste a puces",
                             "Deuxieme point, assez long pour se replier sur deux lignes et "
                             "montrer que le retrait tient sur la continuation",
                             "Troisieme point"}) {
        gw::doc::Paragraph p{item};
        p.list = gw::doc::ListKind::Bullet;
        ps.push_back(p);
    }

    gw::doc::Paragraph h3{"Titre de niveau 3"};
    h3.heading = gw::doc::HeadingLevel::H3;
    ps.push_back(h3);

    for (const char* item : {"Premier element numerote", "Deuxieme element numerote",
                             "Troisieme element numerote"}) {
        gw::doc::Paragraph p{item};
        p.list = gw::doc::ListKind::Numbered;
        ps.push_back(p);
    }

    gw::doc::Paragraph centered{"Ce paragraphe est centre."};
    centered.align = gw::doc::Alignment::Center;
    ps.push_back(centered);

    gw::doc::Paragraph right{"Et celui-ci est aligne a droite."};
    right.align = gw::doc::Alignment::Right;
    ps.push_back(right);

    g_rich.reset(std::move(doc));

    // Curseur pose dans la plage grasse : la barre doit donc montrer Gras
    // allume, et rien d autre du groupe caractere.
    g_rich.set_caret(gw::doc::Position{1, 29}, false);

    g_format_state.bold = true;
    g_format_state.heading = gw::doc::HeadingLevel::None;
    g_format_state.align = gw::doc::Alignment::Left;
    g_format_state.font_name = "Inter";
}

// Texte de demonstration : un paragraphe assez long pour etre replie plusieurs
// fois, un mot plus large que la colonne, et des accents.
void seed_editor() {
    gw::doc::Document doc;
    doc.paragraphs().push_back(gw::doc::Paragraph{
        "Gastinel Write ecrit ses propres octets : ni bibliotheque tierce, ni "
        "gestionnaire de paquets, seulement le SDK du systeme et du code que l on "
        "peut relire."});
    doc.paragraphs().push_back(gw::doc::Paragraph{""});
    doc.paragraphs().push_back(gw::doc::Paragraph{
        "Deuxieme paragraphe, avec des lettres accentuees : eleve, coeur, ile, ou."});
    doc.paragraphs().push_back(gw::doc::Paragraph{
        "Antidisestablishmentarianismesupercalifragilisticexpialidocioussansaucuneespace"
        "nirespirationnicesureniponctuationjusquaudebordementcompletdelacolonnedetexte"});
    g_editor.reset(std::move(doc));

    // Selection posee a cheval sur deux paragraphes : c est le cas ou le
    // dessin de la bande et celui de la coupure de ligne se rencontrent.
    g_editor.set_caret(gw::doc::Position{0, 60}, false);
    g_editor.set_caret(gw::doc::Position{2, 30}, true);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage : %s <repertoire_assets> <repertoire_de_sortie>\n", argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string out = argv[2];

    gw::app::FontSet fonts;
    std::string error;
    if (!fonts.load(assets + "/fonts", error)) {
        std::printf("polices introuvables : %s\n", error.c_str());
        return 1;
    }

    // Jeu de donnees fixe : le rendu doit etre reproductible d une execution a
    // l autre, donc rien n est lu depuis les reglages reels de l utilisateur.
    g_recent = {"C:\\Notes\\journal.odt", "C:\\Notes\\liste de courses.txt",
                "C:\\Notes\\brouillon très long dont le titre est tronqué.odt",
                "C:\\Notes\\reunion.txt"};

    gw::app::TitleBar bar;
    bar.build_icons(1.0f);
    g_home.build_icons();
    g_format_bar.build_icons(1.0f);
    seed_editor();
    seed_rich();
    seed_keyboard();

    gw::render::Framebuffer fb;
    fb.resize(kWidth, kHeight);

    int written = 0;
    for (int dark = 0; dark <= 1; ++dark) {
        gw::app::set_dark_mode(dark != 0);
        const std::string suffix = dark ? "_sombre.bmp" : "_clair.bmp";

        bar.layout(kWidth, 1);

        g_home.layout(kWidth, kHeight, 32, 1, g_recent.size(), fonts);
        compose(fb, fonts, bar, "Gastinel Write", &draw_home);
        written += write_bmp(out + "/accueil" + suffix, fb) ? 1 : 0;

        g_settings_screen.layout(kWidth, kHeight, 32, 1, g_settings, fonts);
        compose(fb, fonts, bar, "Paramètres  —  Gastinel Write", &draw_settings);
        written += write_bmp(out + "/parametres" + suffix, fb) ? 1 : 0;

        g_editor_view.layout(g_editor, kWidth, kHeight, 32, 1, fonts, 16.0f);
        compose(fb, fonts, bar, "demonstration.odt  —  Gastinel Write", &draw_editor);
        written += write_bmp(out + "/editeur" + suffix, fb) ? 1 : 0;

        // Les bandes sont disposees de haut en bas AVANT la vue, comme dans
        // App::refresh_editor_layout : c est la derniere qui dit ou commence le
        // texte.
        g_menu_bar.layout(kWidth, 32, 1, fonts);
        g_format_bar.layout(kWidth, g_menu_bar.bottom(), 1, g_format_state.font_name, fonts);
        g_rich_view.layout(g_rich, kWidth, kHeight, g_format_bar.bottom(), 1, fonts, 16.0f);
        compose(fb, fonts, bar, "mise en forme.odt  —  Gastinel Write", &draw_rich);
        written += write_bmp(out + "/mise_en_forme" + suffix, fb) ? 1 : 0;

        // Mode clavier-first : pas de barre, donc le texte remonte directement
        // sous la barre de titre — c est tout ce qui change dans la mise en
        // page, et c est visible d un coup d oeil sur l image.
        g_keyboard_view.layout(g_keyboard, kWidth, kHeight, g_menu_bar.bottom(), 1, fonts,
                               16.0f);
        compose(fb, fonts, bar, "clavier.odt  —  Gastinel Write", &draw_keyboard);
        written += write_bmp(out + "/clavier_first" + suffix, fb) ? 1 : 0;

        // Menu deroulant ouvert. Il est reconstruit a chaque tour de boucle :
        // ContextMenu rasterise ses libelles a l ouverture, donc un menu ouvert
        // sous le theme clair garderait ses mesures en mode sombre.
        {
            std::vector<gw::app::MenuItem> items;
            items.push_back(gw::app::MenuItem{"Annuler", "Ctrl+Z", 1, true, false});
            items.push_back(gw::app::MenuItem{"Rétablir", "Ctrl+Y", 2, false, false});
            items.push_back(gw::app::MenuItem::make_separator());
            items.push_back(gw::app::MenuItem{"Couper", "Ctrl+X", 3, false, false});
            items.push_back(gw::app::MenuItem{"Copier", "Ctrl+C", 4, false, false});
            items.push_back(gw::app::MenuItem{"Coller", "Ctrl+V", 5, true, false});
            items.push_back(gw::app::MenuItem::make_separator());
            items.push_back(gw::app::MenuItem{"Sélectionner tout", "Ctrl+A", 6, true, false});

            int mx = 0;
            int my = 0;
            g_menu_bar.anchor_for(gw::app::MenuBarMenu::Edit, mx, my);
            g_dropdown.open(std::move(items), mx, my, kWidth, kHeight,
                            fonts.font(gw::app::Weight::Regular),
                            fonts.atlas(gw::app::Weight::Regular, 14.0f));
            g_menu_bar.set_open(gw::app::MenuBarMenu::Edit);

            g_rich_view.layout(g_rich, kWidth, kHeight, g_format_bar.bottom(), 1, fonts, 16.0f);
            compose(fb, fonts, bar, "mise en forme.odt  —  Gastinel Write", &draw_menu_open);
            written += write_bmp(out + "/menu_ouvert" + suffix, fb) ? 1 : 0;

            g_dropdown.close();
            g_menu_bar.set_open(gw::app::MenuBarMenu::None);
        }
    }

    std::printf("%d image(s) ecrite(s) dans %s\n", written, out.c_str());
    return written == 12 ? 0 : 1;
}
