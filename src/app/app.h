// app/app.h — assemblage : fenetre, backend, chrome, ecrans.
//
// Deux vues se partagent la zone cliente de lunique fenetre principale :
// laccueil et lediteur. Ce ne sont pas des fenetres separees, juste deux
// facons de remplir le meme framebuffer — donc aucune transition, aucun
// clignotement, et un seul chemin de rendu.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/chrome.h"
#include "app/context_menu.h"
#include "app/editor_view.h"
#include "app/format_bar.h"
#include "app/fonts.h"
#include "app/home_screen.h"
#include "app/menu_bar.h"
#include "app/recent_files.h"
#include "app/settings.h"
#include "app/settings_screen.h"
#include "app/theme.h"
#include "doc/document.h"
#include "doc/text_edit.h"
#include "platform/backend.h"
#include "platform/window.h"

namespace gw::app {

class App {
public:
    bool init();
    int run();

private:
    enum class Screen {
        Home,
        Editor,
        Settings,
    };

    // --- Evenements fenetre ---
    void on_resize(int width, int height);
    void on_paint();
    bool on_is_caption(int x, int y);
    void on_mouse_move(int x, int y);
    void on_left_down(int x, int y);
    void on_left_up(int x, int y);
    void on_right_down(int x, int y);
    void on_mouse_leave();
    void on_mouse_wheel(int delta);
    void on_left_double_click(int x, int y);
    void on_key_down(platform::Key key, platform::KeyModifiers mods);
    void on_char(std::uint32_t codepoint);
    void on_caret_blink();

    // --- Edition ---
    //
    // Le clavier de l editeur est separe de on_key_down : celui-ci arbitre
    // entre les ecrans, celui-la ne connait que l editeur. Retourne vrai si la
    // touche a ete consommee.
    bool handle_editor_key(platform::Key key, platform::KeyModifiers mods);
    // Toute mutation passe par la : elle refait la mise en page, ramene le
    // curseur dans le champ, le rend visible et redessine. Oublier une de ces
    // quatre etapes donne un bug different a chaque fois — les rassembler
    // supprime la classe entiere.
    void after_edit(bool moved_horizontally = true);
    void refresh_editor_layout();
    void show_caret_now();

    void copy_selection(bool cut);
    void paste_clipboard();

    // --- Rendu ---
    void render_frame();
    void draw_editor();

    // --- Navigation ---
    void relayout();
    void create_new_note();
    void open_note_via_dialog();
    // Prise PAR VALEUR, volontairement : lappelant passe souvent un element
    // de la liste des recents, que open_note reordonne en cours de route. Une
    // reference pointerait alors sur un autre element.
    void open_note(std::string path);
    // Bascule vers l editeur une fois le document en place : mise en page,
    // curseur arme, titre. Commun a la creation et a l ouverture.
    void enter_editor();
    void show_home();
    void show_settings();

    // --- Enregistrement ---
    bool save_note();    // ecrit dans current_path_, ou demande un chemin
    bool save_note_as(); // demande toujours un chemin
    bool write_to(const std::string& path);
    // Vrai si l on peut abandonner le document courant : soit il est propre,
    // soit l utilisateur a tranche. Faux si l utilisateur annule.
    bool confirm_discard_changes();

    // --- Parametres ---
    void run_settings_action(SettingsAction action);
    void import_font_via_dialog();
    // Dossier des polices importees : assets/fonts/custom, a cote de Inter.
    std::string custom_fonts_dir() const;
    // Titre affiche : celui de la barre custom ET celui que voit la barre des
    // taches. Les deux doivent dire la meme chose.
    std::string display_title() const;
    void update_window_title();
    void update_status_line();

    void open_context_menu(int x, int y);
    void run_menu_action(int id);

    // --- Menu principal -----------------------------------------------
    //
    // Le menu deroulant est le meme ContextMenu que le clic droit : seule la
    // liste d entrees change. run_menu_action() est donc unique pour les deux,
    // et chaque entree y appelle exactement la fonction qu appellerait le
    // raccourci clavier equivalent.
    void open_main_menu(MenuBarMenu menu);
    // Ferme le deroulant ET eteint le titre correspondant. Les deux vont
    // toujours ensemble : les separer laisserait un titre allume sous un menu
    // disparu, et c est le genre d etat qu on oublie a un site d appel sur
    // cinq.
    void close_menus();
    // Vrai quand la barre de menu est a l ecran. Comme format_bar_visible(),
    // un seul predicat pour la mise en page, le hit-test et le dessin.
    bool menu_bar_visible() const;

    // --- Actions partagees clavier / menus -----------------------------
    //
    // Trois gestes qu au moins deux chemins d entree declenchent. Les extraire
    // n est pas un raffinement : c est ce qui permet a une entree de menu de
    // n avoir aucune logique propre, comme le chantier l exige.
    void undo_edit();
    void redo_edit();
    void select_all_text();

    // --- Mise en forme -----------------------------------------------
    //
    // La barre d outils ne fait qu emettre une FormatAction ; toute la
    // traduction est ici, et elle appelle exactement les memes fonctions de
    // doc::TextEdit que le clavier appellerait. C est ce qui fait que la mise
    // en forme entre dans l historique d annulation sans mecanisme dedie.
    void run_format_action(FormatAction action);
    // Etat que la barre doit refleter, lu dans TextEdit a chaque dessin.
    FormatState format_state() const;
    // Ouvre la liste des polices sous le selecteur, en reutilisant le menu
    // contextuel : c est deja un menu deroulant complet (clavier, survol,
    // fermeture au clic exterieur, recadrage aux bords de la fenetre), et en
    // ecrire un second n apporterait qu une seconde serie de bugs.
    void open_font_menu();
    // Declare aupres du moteur de polices toutes les polices connues des
    // reglages, pour que le rendu puisse honorer un nom pose dans un document.
    void sync_registered_fonts();
    // Vrai quand la barre de mise en forme est a l ecran : dans l editeur, et
    // seulement hors mode clavier-first. Un seul predicat pour la mise en page,
    // le hit-test et le dessin — trois endroits qui, s ils repondaient chacun
    // pour eux-memes, finiraient par ne plus repondre pareil.
    bool format_bar_visible() const;

    // Haut de la zone de texte : sous la barre de titre, et sous la barre de
    // mise en forme quand l editeur est a l ecran.
    int editor_top() const;

    // Marge entre le bord de la fenetre et le contenu : 1 px pour le filet
    // quon dessine soi-meme (il ny a plus de cadre systeme), 0 une fois
    // maximisee.
    int frame_inset() const;

    // Corps du texte de l editeur, en pixels. Lu dans les reglages a chaque
    // appel et jamais memorise : l atlas correspondant est cree a la demande
    // par FontSet, donc changer la taille n a aucune invalidation a orchestrer.
    float body_size() const;

    platform::Window window_;
    std::unique_ptr<platform::IPresentBackend> backend_;

    FontSet fonts_;
    render::Framebuffer framebuffer_;

    TitleBar title_bar_;
    MenuBar menu_bar_;
    ContextMenu context_menu_;
    HomeScreen home_;
    SettingsScreen settings_screen_;
    RecentFiles recent_;
    Settings settings_;

    EditorView editor_view_;
    FormatBar format_bar_;

    Screen screen_ = Screen::Home;
    doc::TextEdit editor_;
    std::string current_path_; // vide pour une note jamais enregistree

    // Curseur de saisie : etat du clignotement, et vrai pendant un
    // glisser-selectionner a la souris.
    bool caret_visible_ = true;
    bool selecting_ = false;

    std::string assets_dir_;
    std::string status_line_;
    std::string editor_notice_; // message transitoire affiche sous le contenu
};

} // namespace gw::app
