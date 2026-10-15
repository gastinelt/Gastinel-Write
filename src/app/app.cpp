#include "app/app.h"

#include "app/quotes.h"
#include "app/splash.h"
#include "app/theme.h"
#include "doc/markdown_import.h"
#include "doc/odt_io.h"
#include "doc/txt_io.h"
#include "platform/clipboard.h"
#include "platform/dialogs.h"
#include "platform/log.h"
#include "platform/system.h"
#include "render/draw.h"
#include "render/text_run.h"

#include <chrono>
#include <cstdlib>

namespace gw::app {
namespace {

constexpr float kUiPixelSize = 14.0f;
constexpr int kEditorMarginX = 48;
constexpr int kEditorMarginTop = 22;

// Identifiants des entrees du menu contextuel.
enum MenuAction {
    kActionCut = 1,
    kActionCopy,
    kActionPaste,
    kActionSelectAll,
    kActionUndo,
    kActionRedo,

    // Entrees du menu Fichier et du menu Affichage. Elles partagent la table
    // du menu contextuel parce qu elles partagent son gestionnaire : un
    // identifiant est un identifiant, quel que soit le menu qui l a montre.
    kActionNew,
    kActionOpen,
    kActionSave,
    kActionSaveAs,
    kActionHome,
    kActionToggleDark,
    kActionToggleKeyboard,

    // Les entrees du selecteur de police sont numerotees a partir de cette
    // base : leur identifiant porte l indice dans Settings::fonts(). Une plage
    // reservee plutot qu un enum par police, parce que la liste est construite
    // a l execution.
    kActionFontBase = 100,
};

// Cherche le dossier assets relativement a lexecutable, jamais au repertoire
// courant : lancer le binaire depuis ailleurs ne doit pas casser le
// chargement, et le dialogue de fichier peut changer le repertoire courant.
std::string locate_assets() {
    const std::string base = platform::executable_directory();
    const char* candidates[] = {"/assets", "/../assets", "/../../assets"};
    for (const char* suffix : candidates) {
        const std::string path = base + suffix;
        if (platform::directory_exists(path)) return path;
    }
    return {};
}

} // namespace

bool App::init() {
    assets_dir_ = locate_assets();
    if (assets_dir_.empty()) {
        platform::log_error("Dossier assets/ introuvable a cote de lexecutable.");
        return false;
    }

    // 1. Polices. Elles viennent en premier parce que lecran de chargement
    //    affiche du texte rendu par notre propre pipeline : sans police, il
    //    naurait rien a dessiner. Cf. lentete de splash.h.
    std::string error;
    if (!fonts_.load(assets_dir_ + "/fonts", error)) {
        platform::log_error("Chargement des polices impossible : %s", error.c_str());
        return false;
    }
    platform::log("Polices chargees depuis %s/fonts", assets_dir_.c_str());

    // 2. Reglages, avant le splash : celui-ci est le premier pixel affiche, il
    //    doit deja etre au bon theme. Un chargement place apres provoquerait un
    //    flash blanc a chaque demarrage en mode sombre.
    settings_.load(custom_fonts_dir());
    platform::log("Reglages charges (%s, corps %d, %zu police(s))",
                  settings_.dark_mode() ? "mode sombre" : "mode clair",
                  settings_.default_font_size(), settings_.fonts().size());

    // 3. Ecran de chargement, avant toute initialisation graphique lourde.
    const auto splash_start = std::chrono::steady_clock::now();
    SplashScreen splash;
    const Quote quote = get_quote_of_the_day();
    splash.show(fonts_, assets_dir_ + "/branding", settings_.show_quote_of_the_day(), &quote);

    // 4. Initialisation lourde : fenetre, backend, atlas, liste des recents.
    recent_.load();
    platform::log("Projets recents : %zu entree(s) depuis %s", recent_.entries().size(),
                  RecentFiles::storage_path().c_str());

    // Les polices importees doivent etre connues du moteur de rendu avant le
    // premier dessin : un document ouvert au demarrage peut deja en nommer une.
    sync_registered_fonts();

    if (!window_.create(L"Gastinel Write", 1000, 700)) return false;

    const float dpi_scale = window_.dpi_scale();
    title_bar_.build_icons(dpi_scale);
    format_bar_.build_icons(dpi_scale);
    home_.build_icons();

    backend_ = platform::create_backend(window_.handle());
    if (!backend_) return false;

    update_status_line();

    window_.on_resize = [this](int w, int h) { on_resize(w, h); };
    window_.on_paint = [this] { on_paint(); };
    window_.on_is_caption = [this](int x, int y) { return on_is_caption(x, y); };
    window_.on_mouse_move = [this](int x, int y) { on_mouse_move(x, y); };
    window_.on_left_down = [this](int x, int y) { on_left_down(x, y); };
    window_.on_left_up = [this](int x, int y) { on_left_up(x, y); };
    window_.on_right_down = [this](int x, int y) { on_right_down(x, y); };
    window_.on_mouse_leave = [this] { on_mouse_leave(); };
    window_.on_mouse_wheel = [this](int delta) { on_mouse_wheel(delta); };
    window_.on_left_double_click = [this](int x, int y) { on_left_double_click(x, y); };
    window_.on_key_down = [this](platform::Key k, platform::KeyModifiers m) { on_key_down(k, m); };
    window_.on_char = [this](std::uint32_t cp) { on_char(cp); };
    window_.on_caret_blink = [this] { on_caret_blink(); };
    window_.on_animation_tick = [this] {
        if (screen_ != Screen::Editor) {
            window_.set_animation_timer(false);
            return;
        }
        const bool still_running = editor_view_.step_animation(0.016f);
        if (!still_running) {
            window_.set_animation_timer(false);
        }
        window_.request_repaint();
    };

    window_.sync_client_size();

    // 5. Ouverture eventuelle de la derniere note ouverte si l'option est activee
    if (settings_.open_last_note() && !recent_.entries().empty()) {
        open_note(recent_.entries().front());
    }

    // 6. Duree d'affichage du splash screen (citation du jour ou inspection)
    int target_hold_ms = 0;
    if (settings_.show_quote_of_the_day()) {
        if (settings_.quote_duration() == Settings::QuoteDuration::FiveSeconds) {
            target_hold_ms = 5000;
        } else if (settings_.quote_duration() == Settings::QuoteDuration::FifteenSeconds) {
            target_hold_ms = 15000;
        }
    }

    const std::string hold = platform::environment_variable("GASTINEL_SPLASH_HOLD_MS");
    if (!hold.empty()) {
        const int ms = std::atoi(hold.c_str());
        if (ms > 0) target_hold_ms = ms;
    }

    if (target_hold_ms > 0) {
        while (splash.is_open() && !splash.dismissed()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - splash_start).count();
            if (elapsed >= target_hold_ms) break;
            splash.pump_events();
            platform::sleep_ms(16);
        }
    }

    splash.close();
    return true;
}

int App::run() {
    window_.show();
    return window_.run_message_loop();
}

int App::frame_inset() const { return window_.is_maximized() ? 0 : 1; }

float App::body_size() const { return static_cast<float>(settings_.default_font_size()); }

void App::relayout() {
    const int inset = frame_inset();
    title_bar_.layout(framebuffer_.width, inset);
    home_.layout(framebuffer_.width, framebuffer_.height, title_bar_.height(), inset,
                 recent_.entries().size(), fonts_);
    settings_screen_.layout(framebuffer_.width, framebuffer_.height, title_bar_.height(), inset,
                            settings_, fonts_);
    if (screen_ == Screen::Editor) {
        refresh_editor_layout();
    }
}

bool App::format_bar_visible() const {
    return screen_ == Screen::Editor && !settings_.keyboard_first();
}

bool App::menu_bar_visible() const {
    // Editeur seulement. L accueil porte deja ses propres boutons pour tout ce
    // que le menu Fichier propose, et le menu Edition n aurait rien a y
    // commander : une barre de menu grisee aux trois quarts vaut moins que pas
    // de barre du tout.
    return screen_ == Screen::Editor;
}

int App::editor_top() const {
    // Trois bandes empilees au maximum : titre, menu, mise en forme. Chacune
    // peut manquer, et le texte commence sous la derniere presente.
    if (format_bar_visible()) return format_bar_.bottom();
    if (menu_bar_visible()) return menu_bar_.bottom();
    return title_bar_.height();
}

void App::refresh_editor_layout() {
    // La barre est disposee avant la vue : c est elle qui dit ou commence le
    // texte, et la mesurer apres coup donnerait une zone de texte decalee d une
    // frame a chaque changement de largeur.
    menu_bar_.layout(framebuffer_.width, title_bar_.height(), frame_inset(), fonts_);
    // Disposee meme quand elle est masquee : ses rectangles restent alors
    // coherents, et rien ne depend d un etat calcule sous un autre mode. Le
    // cout est une poignee de mesures de texte par frame d edition.
    format_bar_.layout(framebuffer_.width, menu_bar_.bottom(), frame_inset(),
                       format_state().font_name, fonts_);
    editor_view_.layout(editor_, framebuffer_.width, framebuffer_.height, editor_top(),
                        frame_inset(), fonts_,
                        static_cast<float>(settings_.default_font_size()));
}

void App::sync_registered_fonts() {
    fonts_.clear_registered_fonts();
    for (const FontEntry& entry : settings_.fonts()) {
        if (entry.built_in || entry.file_name.empty()) continue;
        fonts_.register_font(entry.display_name, custom_fonts_dir() + "/" + entry.file_name);
    }
}

void App::on_resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    framebuffer_.resize(width, height);
    backend_->resize(width, height);
    relayout();

    // Un menu ouvert pendant un redimensionnement se retrouverait a une
    // position qui na plus de sens.
    close_menus();
    window_.request_repaint();
}

void App::on_paint() {
    if (framebuffer_.empty()) return;
    render_frame();
    backend_->present(framebuffer_);
}

// --- Navigation ------------------------------------------------------------

std::string App::display_title() const {
    if (screen_ == Screen::Settings) return std::string(i18n::tr(i18n::StringId::SettingsTitle)) + "  —  Gastinel Write";
    if (screen_ != Screen::Editor) return "Gastinel Write";
    const std::string name =
        current_path_.empty() ? std::string(i18n::tr(i18n::StringId::UntitledDoc)) : file_name_of(current_path_);
    // Asterisque de document modifie, a la place d un indicateur separe : le
    // titre est deja lu par la barre des taches et Alt+Tab, donc l information
    // arrive aussi la ou l utilisateur regarde quand il quitte l application.
    const std::string mark = editor_.document().is_dirty() ? "*" : "";
    return mark + name + "  —  Gastinel Write";
}

void App::update_window_title() { window_.set_title(display_title()); }

void App::update_status_line() {
    if (!backend_) return;
    status_line_ = std::string(i18n::tr(i18n::StringId::StatusBackend)) + " : " +
                   platform::backend_name(backend_->kind()) + "  |  " +
                   i18n::tr(i18n::StringId::StatusShortcuts);
}

void App::show_home() {
    // Quitter l editeur avec des modifications non enregistrees doit poser la
    // question. Depuis les parametres, il n y a rien a perdre : la question ne
    // se pose que sur le chemin qui vient de l editeur.
    if (screen_ == Screen::Editor && !confirm_discard_changes()) return;

    screen_ = Screen::Home;
    window_.set_caret_blinking(false);
    window_.set_animation_timer(false);
    window_.set_cursor_shape(platform::Window::Cursor::Arrow);
    home_.clear_cursor();
    relayout();
    update_window_title();
    window_.request_repaint();
}

void App::show_settings() {
    screen_ = Screen::Settings;
    window_.set_caret_blinking(false);
    window_.set_animation_timer(false);
    window_.set_cursor_shape(platform::Window::Cursor::Arrow);
    settings_screen_.clear_cursor();
    settings_screen_.clear_notice();
    relayout();
    update_window_title();
    window_.request_repaint();
}

std::string App::custom_fonts_dir() const { return assets_dir_ + "/fonts/custom"; }

void App::enter_editor() {
    screen_ = Screen::Editor;
    editor_view_.scroll_to_top();
    editor_view_.invalidate_desired_x();
    editor_view_.reset_caret_animation();
    refresh_editor_layout();
    show_caret_now();
    window_.set_caret_blinking(true);
    window_.set_animation_timer(false);
    update_window_title();
    window_.request_repaint();
}

void App::create_new_note() {
    if (!confirm_discard_changes()) return;

    editor_.reset(doc::Document{});
    current_path_.clear();
    editor_notice_.clear();
    enter_editor();
    platform::log("Nouvelle note creee (en memoire, sans fichier)");
}

void App::open_note(std::string path) {
    if (!confirm_discard_changes()) return;

    const std::string ext = extension_of(path);
    std::string error;
    bool ok = false;

    // Le document est charge a cote, pas dans celui qui est ouvert : un echec
    // de lecture ne doit pas detruire ce que l utilisateur a sous les yeux.
    doc::Document loaded;
    if (ext == ".odt") {
        ok = doc::load_odt(path, loaded, error);
    } else {
        // Tout ce qui nest pas .odt est traite comme du texte brut, y compris
        // une extension inconnue : cest le comportement le moins surprenant.
        ok = doc::load_txt(path, loaded, error);
    }

    if (!ok) {
        platform::log_error("Ouverture impossible : %s", error.c_str());
        editor_notice_ = "Ouverture impossible : " + error;
        // On reste sur laccueil : basculer vers un editeur vide masquerait
        // lechec.
        show_home();
        return;
    }

    editor_.reset(std::move(loaded));
    current_path_ = path;
    recent_.touch(path);
    editor_notice_.clear();
    relayout(); // la liste des recents a change, la grille aussi
    enter_editor();
    platform::log("Note ouverte : %s (%zu paragraphes)", path.c_str(),
                  editor_.document().paragraphs().size());
}

void App::open_note_via_dialog() {
    std::string path;
    if (!platform::prompt_open_document(window_.handle(), path)) {
        platform::log("Ouverture annulee par lutilisateur");
        return;
    }
    open_note(path);
}

// --- Entrees ---------------------------------------------------------------

bool App::on_is_caption(int x, int y) {
    if (context_menu_.contains(x, y)) return false;
    return title_bar_.is_caption_point(x, y);
}

void App::on_mouse_move(int x, int y) {
    if (context_menu_.is_open()) {
        if (context_menu_.on_mouse_move(x, y) == MenuResult::Consumed) {
            window_.request_repaint();
        }
        return;
    }

    // Extension de la selection en cours de glissement. Traitee avant tout le
    // reste : pendant un glisser, rien dautre ne doit reagir au survol.
    if (selecting_) {
        editor_.set_caret(editor_view_.position_at_point(editor_, x, y, fonts_, body_size()),
                          true);
        after_edit();
        return;
    }

    if (screen_ == Screen::Editor) {
        window_.set_cursor_shape(editor_view_.contains_point(x, y)
                                     ? platform::Window::Cursor::Text
                                     : platform::Window::Cursor::Arrow);
        if (format_bar_visible() && format_bar_.set_cursor(x, y)) window_.request_repaint();
        if (menu_bar_.set_cursor(x, y)) window_.request_repaint();
    }

    const CaptionButton previous = title_bar_.hovered();
    const CaptionButton current = title_bar_.button_at(x, y);
    bool dirty = false;
    if (current != previous) {
        title_bar_.set_hovered(current);
        dirty = true;
    }

    // Au-dessus de la barre de titre, aucun element de contenu nest
    // survolable : on nettoie pour ne pas laisser un bouton allume.
    const bool above_content = y < title_bar_.height();
    if (screen_ == Screen::Home) {
        if (above_content) {
            home_.clear_cursor();
            dirty = true;
        } else if (home_.set_cursor(x, y)) {
            dirty = true;
        }
    } else if (screen_ == Screen::Settings) {
        if (above_content) {
            settings_screen_.clear_cursor();
            dirty = true;
        } else if (settings_screen_.set_cursor(x, y)) {
            dirty = true;
        }
    }

    if (dirty) window_.request_repaint();
}

void App::on_left_down(int x, int y) {
    // LA BARRE DE MENU AGIT A L ENFONCEMENT, contrairement a tous les autres
    // boutons de l application. C est la convention de toutes les barres de
    // menu, et surtout c est ce qui permet d ouvrir un menu, de glisser jusqu a
    // l entree voulue et de relacher dessus : agir au relachement rendrait ce
    // geste impossible.
    if (menu_bar_visible()) {
        const MenuBarMenu hit = menu_bar_.menu_at(x, y);
        if (hit != MenuBarMenu::None) {
            // Recliquer sur le titre deja ouvert referme, au lieu de rouvrir le
            // meme menu — sinon il n y aurait aucun moyen de le fermer en
            // visant la ou on vient de cliquer.
            const bool same = context_menu_.is_open() && menu_bar_.open() == hit;
            close_menus();
            if (!same) open_main_menu(hit);
            window_.request_repaint();
            return;
        }
        // Ailleurs dans la bande : rien a faire, mais surtout pas de
        // glisser-selectionner dessous.
        if (menu_bar_.contains_point(x, y)) {
            close_menus();
            window_.request_repaint();
            return;
        }
    }

    if (context_menu_.is_open()) {
        if (!context_menu_.contains(x, y)) {
            close_menus();
            window_.request_repaint();
        }
        return;
    }

    const CaptionButton hit = title_bar_.button_at(x, y);
    if (hit != CaptionButton::None) {
        title_bar_.set_pressed(hit);
        title_bar_.set_hovered(hit);
        window_.capture_mouse();
        window_.request_repaint();
        return;
    }

    // La barre de mise en forme agit au RELACHEMENT, comme tous les boutons de
    // l application : on l intercepte ici uniquement pour ne pas demarrer un
    // glisser-selectionner sous elle.
    if (format_bar_visible() && format_bar_.contains_point(x, y)) return;

    // Debut dun glisser-selectionner. La capture est indispensable : sans
    // elle, sortir de la fenetre pendant la selection ferait perdre le
    // relachement et le glissement resterait colle au curseur.
    if (screen_ == Screen::Editor && editor_view_.contains_point(x, y)) {
        editor_.set_caret(editor_view_.position_at_point(editor_, x, y, fonts_, body_size()),
                          false);
        selecting_ = true;
        window_.capture_mouse();
        after_edit();
    }
}

void App::on_left_double_click(int x, int y) {
    if (context_menu_.is_open()) return;
    if (screen_ != Screen::Editor || !editor_view_.contains_point(x, y)) return;
    if (format_bar_visible() && format_bar_.contains_point(x, y)) return;
    if (menu_bar_visible() && menu_bar_.contains_point(x, y)) return;

    editor_.select_word_at(editor_view_.position_at_point(editor_, x, y, fonts_, body_size()));
    after_edit();
}

void App::on_left_up(int x, int y) {
    if (context_menu_.is_open()) {
        switch (context_menu_.on_left_up(x, y)) {
            case MenuResult::Chosen: {
                const int id = context_menu_.chosen_id();
                close_menus();
                run_menu_action(id);
                window_.request_repaint();
                break;
            }
            case MenuResult::Dismissed:
                close_menus();
                window_.request_repaint();
                break;
            default:
                break;
        }
        return;
    }

    if (selecting_) {
        selecting_ = false;
        window_.release_mouse();
        return;
    }

    const CaptionButton pressed = title_bar_.pressed();
    if (pressed != CaptionButton::None) {
        title_bar_.set_pressed(CaptionButton::None);
        window_.release_mouse();

        // Le bouton nagit que si le relachement a lieu sur le meme bouton que
        // lenfoncement, comme un bouton systeme.
        if (title_bar_.button_at(x, y) == pressed) {
            switch (pressed) {
                case CaptionButton::Minimize: window_.minimize(); break;
                case CaptionButton::MaximizeRestore: window_.toggle_maximize(); break;
                case CaptionButton::Close: window_.close(); return;
                default: break;
            }
        }
        window_.request_repaint();
        return;
    }

    if (y < title_bar_.height()) return;

    if (screen_ == Screen::Editor) {
        // La bande de menu a deja tout fait a l enfoncement.
        if (menu_bar_.contains_point(x, y)) return;
        if (!format_bar_visible()) return;
        const FormatAction action = format_bar_.action_at(x, y);
        if (action != FormatAction::None) run_format_action(action);
        return;
    }

    if (screen_ == Screen::Settings) {
        run_settings_action(settings_screen_.action_at(x, y));
        return;
    }
    if (screen_ != Screen::Home) return;

    switch (home_.action_at(x, y)) {
        case HomeAction::Settings:
            show_settings();
            break;
        case HomeAction::CreateNote:
            create_new_note();
            break;
        case HomeAction::OpenNote:
            open_note_via_dialog();
            break;
        case HomeAction::OpenRecent: {
            const int index = home_.selected_recent();
            if (index >= 0 && static_cast<std::size_t>(index) < recent_.entries().size()) {
                open_note(recent_.entries()[static_cast<std::size_t>(index)]);
            }
            break;
        }
        case HomeAction::None:
            break;
    }
}

void App::on_right_down(int x, int y) {
    if (context_menu_.is_open()) {
        close_menus();
        window_.request_repaint();
        return;
    }
    // Le menu contextuel appartient a la zone de texte : ni la barre de titre,
    // ni laccueil ne le portent.
    if (screen_ != Screen::Editor) return;
    if (y < title_bar_.height()) return;
    if (format_bar_visible() && format_bar_.contains_point(x, y)) return;
    if (menu_bar_visible() && menu_bar_.contains_point(x, y)) return;
    open_context_menu(x, y);
}

void App::on_mouse_leave() {
    bool dirty = false;
    if (title_bar_.hovered() != CaptionButton::None &&
        title_bar_.pressed() == CaptionButton::None) {
        title_bar_.set_hovered(CaptionButton::None);
        dirty = true;
    }
    if (screen_ == Screen::Home) {
        home_.clear_cursor();
        dirty = true;
    } else if (screen_ == Screen::Settings) {
        settings_screen_.clear_cursor();
        dirty = true;
    } else if (screen_ == Screen::Editor) {
        format_bar_.clear_cursor();
        menu_bar_.clear_cursor();
        dirty = true;
    }
    if (dirty) window_.request_repaint();
}

void App::on_mouse_wheel(int delta) {
    if (context_menu_.is_open()) return;
    if (screen_ == Screen::Editor) {
        // Le defilement ne bouge pas le curseur : la vue et le point d insertion
        // sont deux choses distinctes, exactement comme dans un traitement de
        // texte. La frappe suivante ramenera la vue sur le curseur.
        if (editor_view_.scroll_by(delta)) {
            window_.set_animation_timer(false);
            window_.request_repaint();
        }
        return;
    }
    if (screen_ != Screen::Home) return;
    if (home_.scroll_by(delta)) window_.request_repaint();
}

void App::on_key_down(platform::Key key, platform::KeyModifiers mods) {
    if (context_menu_.is_open()) {
        if (context_menu_.on_key_down(key) == MenuResult::Dismissed) {
            close_menus();
            window_.request_repaint();
        }
        return;
    }

    if (screen_ == Screen::Editor && handle_editor_key(key, mods)) return;

    // Echap ramene a laccueil depuis lediteur comme depuis les parametres :
    // les deux sont des ecrans dont laccueil est le parent naturel. Il ne
    // ferme plus lapplication : maintenant quune note peut etre ouverte, une
    // frappe malheureuse ne doit pas tout faire disparaitre.
    if (key == platform::Key::Escape && screen_ != Screen::Home) show_home();
}

// --- Clavier de lediteur ---------------------------------------------------

bool App::handle_editor_key(platform::Key key, platform::KeyModifiers mods) {
    using platform::Key;

    // Alt est reserve aux raccourcis systeme et, plus tard, au menu principal.
    // On ne consomme rien pour ne pas le confisquer d avance.
    if (mods.alt) return false;

    const bool extend = mods.shift;
    const bool by_word = mods.ctrl;

    if (mods.ctrl) {
        switch (key) {
            case Key::S:
                if (mods.shift) {
                    save_note_as();
                } else {
                    save_note();
                }
                return true;
            case Key::A:
                select_all_text();
                return true;
            case Key::C:
                copy_selection(false);
                return true;
            case Key::X:
                copy_selection(true);
                return true;
            case Key::V:
                paste_clipboard();
                return true;
            case Key::Z:
                // Ctrl+Maj+Z retablit, comme Ctrl+Y : les deux conventions
                // coexistent selon les logiciels, et rien n oblige a choisir.
                if (mods.shift) {
                    redo_edit();
                } else {
                    undo_edit();
                }
                return true;
            // Les trois raccourcis universels de mise en forme. Ils appellent
            // run_format_action et non TextEdit directement, pour que le
            // clavier et la barre d outils empruntent strictement le meme
            // chemin — y compris le rafraichissement de la barre.
            case Key::B:
                run_format_action(FormatAction::Bold);
                return true;
            case Key::I:
                run_format_action(FormatAction::Italic);
                return true;
            case Key::U:
                run_format_action(FormatAction::Underline);
                return true;
            case Key::Y:
                redo_edit();
                return true;
            default:
                break;
        }
    }

    switch (key) {
        case Key::Left:
            editor_.move_left(extend, by_word);
            after_edit();
            return true;
        case Key::Right:
            editor_.move_right(extend, by_word);
            after_edit();
            return true;

        // Les deplacements verticaux passent par la vue : « la ligne du
        // dessus » n existe pas dans le modele de texte.
        case Key::Up:
            editor_.set_caret(editor_view_.position_above(editor_, fonts_, body_size()), extend);
            after_edit(false);
            return true;
        case Key::Down:
            editor_.set_caret(editor_view_.position_below(editor_, fonts_, body_size()), extend);
            after_edit(false);
            return true;
        case Key::PageUp:
            editor_.set_caret(editor_view_.position_page(editor_, fonts_, body_size(), -1), extend);
            after_edit(false);
            return true;
        case Key::PageDown:
            editor_.set_caret(editor_view_.position_page(editor_, fonts_, body_size(), +1), extend);
            after_edit(false);
            return true;

        case Key::Home:
            if (mods.ctrl) {
                editor_.move_document_start(extend);
            } else {
                // Debut de la ligne *affichee*, pas du paragraphe : sur un
                // paragraphe replie sur cinq lignes, l autre choix ferait
                // sauter le curseur quatre lignes plus haut.
                editor_.set_caret(editor_view_.line_start(editor_), extend);
            }
            after_edit();
            return true;
        case Key::End:
            if (mods.ctrl) {
                editor_.move_document_end(extend);
            } else {
                editor_.set_caret(editor_view_.line_end(editor_), extend);
            }
            after_edit();
            return true;

        case Key::Backspace:
            editor_.erase_backward(by_word);
            after_edit();
            return true;
        case Key::Delete:
            editor_.erase_forward(by_word);
            after_edit();
            return true;
        case Key::Enter:
            editor_.insert_paragraph_break();
            after_edit();
            return true;

        case Key::Tab:
            // Quatre espaces plutot qu une tabulation : le modele de document
            // n a pas de taquets, et une tabulation reelle s afficherait comme
            // un glyphe manquant de largeur nulle. C est une limite assumee,
            // documentee dans le README.
            editor_.insert_text("    ");
            after_edit();
            return true;

        case Key::Escape:
            // Une selection en cours se replie d abord. Sortir de l editeur au
            // premier Echap alors qu on voulait juste deselectionner serait
            // brutal.
            if (editor_.has_selection()) {
                editor_.set_caret(editor_.caret(), false);
                after_edit();
                return true;
            }
            return false;

        default:
            return false;
    }
}

void App::on_char(std::uint32_t codepoint) {
    if (screen_ != Screen::Editor || context_menu_.is_open()) return;

    // Encodage UTF-8 du point de code. C est le seul endroit du programme qui
    // encode : partout ailleurs, l UTF-8 arrive deja forme d un fichier ou du
    // presse-papiers.
    std::string utf8;
    if (codepoint < 0x80) {
        utf8.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        utf8.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        utf8.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        utf8.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    editor_.insert_text(utf8);

    // Mode clavier-first : le caractere qui vient d etre pose ferme peut-etre
    // une syntaxe. C est le SEUL point d entree de la conversion — elle suit
    // une frappe reelle, jamais un collage ni une ouverture de fichier, ou elle
    // mangerait des marqueurs que l utilisateur n a pas tapes maintenant.
    if (settings_.keyboard_first()) editor_.apply_markdown_live();

    after_edit();
}

void App::on_caret_blink() {
    if (screen_ != Screen::Editor) return;
    if (editor_view_.is_animating()) {
        caret_visible_ = true;
    } else {
        caret_visible_ = !caret_visible_;
    }
    window_.request_repaint();
}

void App::show_caret_now() {
    // Le curseur est rendu visible a chaque action : un curseur qui clignote
    // pendant la frappe donne l impression d un caractere perdu.
    caret_visible_ = true;
}

void App::after_edit(bool moved_horizontally) {
    if (moved_horizontally) editor_view_.invalidate_desired_x();
    refresh_editor_layout();
    editor_view_.ensure_caret_visible(editor_);
    editor_view_.update_caret_target(editor_, fonts_, body_size(), settings_.smooth_caret());
    if (editor_view_.is_animating()) {
        window_.set_animation_timer(true);
    }
    show_caret_now();
    update_window_title();
    window_.request_repaint();
}

// --- Presse-papiers ---------------------------------------------------------

void App::undo_edit() {
    editor_.undo();
    after_edit();
}

void App::redo_edit() {
    editor_.redo();
    after_edit();
}

void App::select_all_text() {
    editor_.select_all();
    after_edit();
}

void App::copy_selection(bool cut) {
    if (!editor_.has_selection()) return;
    if (!platform::clipboard_set_text(editor_.selected_text())) {
        // Un presse-papiers verrouille par un autre processus est courant et
        // sans gravite. On ne coupe surtout pas dans ce cas : le texte
        // n irait nulle part.
        editor_notice_ = "Presse-papiers indisponible.";
        window_.request_repaint();
        return;
    }
    if (cut) {
        // Avec une selection en cours, erase_forward la supprime en bloc et
        // enregistre l annulation. C est le meme chemin que Suppr : passer par
        // TextEdit::delete_selection directement sauterait l historique.
        editor_.erase_forward(false);
        after_edit();
    }
}

void App::paste_clipboard() {
    std::string text;
    if (!platform::clipboard_get_text(text) || text.empty()) return;

    // Un texte colle est lu comme du Markdown complet : titres, listes, gras,
    // italique, liens... Un texte sans aucune syntaxe ressort identique et reste
    // sur le chemin de frappe d origine, avec le style arme eventuel.
    std::vector<doc::Paragraph> blocks = doc::markdown_to_blocks(text);
    const bool plain_text = blocks.size() == 1 && blocks[0].is_plain() &&
                            blocks[0].text.size() == text.size();
    if (plain_text) {
        editor_.insert_text(text);
    } else {
        editor_.insert_blocks(std::move(blocks));
    }
    after_edit();
}

// --- Enregistrement ---------------------------------------------------------

bool App::write_to(const std::string& path) {
    std::string error;
    const bool ok = extension_of(path) == ".odt"
                        ? doc::save_odt(path, editor_.document(), error)
                        : doc::save_txt(path, editor_.document(), error);
    if (!ok) {
        platform::log_error("Enregistrement impossible : %s", error.c_str());
        editor_notice_ = "Enregistrement impossible : " + error;
        window_.request_repaint();
        return false;
    }

    editor_.document().set_dirty(false);
    current_path_ = path;
    recent_.touch(path);
    editor_notice_ = "Enregistré : " + file_name_of(path);
    relayout();
    update_window_title();
    window_.request_repaint();
    platform::log("Note enregistree : %s", path.c_str());
    return true;
}

bool App::save_note() {
    if (current_path_.empty()) return save_note_as();
    return write_to(current_path_);
}

bool App::save_note_as() {
    std::string path;
    const std::string suggested =
        current_path_.empty() ? std::string("note") : file_name_of(current_path_);
    if (!platform::prompt_save_document(window_.handle(), suggested, path)) {
        platform::log("Enregistrement annule par lutilisateur");
        return false;
    }
    return write_to(path);
}

bool App::confirm_discard_changes() {
    if (!editor_.document().is_dirty()) return true;

    const std::string name =
        current_path_.empty() ? std::string("Nouvelle note") : file_name_of(current_path_);
    switch (platform::prompt_discard_changes(window_.handle(), name)) {
        case platform::ConfirmResult::Yes:
            // Si l enregistrement echoue ou est annule, on ne quitte pas : ce
            // serait perdre le document apres avoir promis de le garder.
            return save_note();
        case platform::ConfirmResult::No:
            return true;
        case platform::ConfirmResult::Cancel:
            return false;
    }
    return false;
}

void App::close_menus() {
    context_menu_.close();
    menu_bar_.set_open(MenuBarMenu::None);
}

void App::open_context_menu(int x, int y) {
    // Couper et copier n ont de sens qu avec une selection : les griser dit
    // pourquoi rien ne se passerait, la ou une entree active qui ne fait rien
    // laisserait croire a une panne.
    const bool has_selection = editor_.has_selection();

    std::vector<MenuItem> items;
    items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionCut), "Ctrl+X", kActionCut, has_selection, false});
    items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionCopy), "Ctrl+C", kActionCopy, has_selection, false});
    items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionPaste), "Ctrl+V", kActionPaste, true, false});
    items.push_back(MenuItem::make_separator());
    items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionSelectAll), "Ctrl+A", kActionSelectAll, true, false});

    context_menu_.open(std::move(items), x, y, framebuffer_.width, framebuffer_.height,
                       fonts_.font(Weight::Regular), fonts_.atlas(Weight::Regular, kUiPixelSize));
    window_.request_repaint();
}

void App::run_menu_action(int id) {
    // Les entrees du menu ne portent aucune logique propre : elles appellent
    // exactement les memes fonctions que les raccourcis clavier. C est la
    // condition pour que les deux chemins ne divergent jamais.
    if (id >= kActionFontBase) {
        const auto index = static_cast<std::size_t>(id - kActionFontBase);
        if (index < settings_.fonts().size()) {
            editor_.set_font(settings_.fonts()[index].display_name);
            after_edit();
        }
        return;
    }

    switch (id) {
        case kActionCut: copy_selection(true); break;
        case kActionCopy: copy_selection(false); break;
        case kActionPaste: paste_clipboard(); break;
        case kActionSelectAll: select_all_text(); break;
        case kActionUndo: undo_edit(); break;
        case kActionRedo: redo_edit(); break;

        case kActionNew: create_new_note(); break;
        case kActionOpen: open_note_via_dialog(); break;
        case kActionSave: save_note(); break;
        case kActionSaveAs: save_note_as(); break;
        case kActionHome: show_home(); break;

        // Les deux bascules d affichage passent par run_settings_action et non
        // par Settings directement : c est ce chemin qui persiste le reglage,
        // refait la mise en page et redessine. Le menu ne doit rien savoir de
        // ces trois etapes.
        case kActionToggleDark: run_settings_action(SettingsAction::ToggleDarkMode); break;
        case kActionToggleKeyboard:
            run_settings_action(SettingsAction::ToggleKeyboardFirst);
            break;

        default: break;
    }
}

void App::open_main_menu(MenuBarMenu menu) {
    // Coche dans la colonne du raccourci pour les entrees a etat, comme le
    // selecteur de police : le menu n a pas de colonne dediee, et en ajouter
    // une pour trois entrees ne se justifie pas.
    const auto mark = [](bool on) { return on ? std::string("•") : std::string(); };

    std::vector<MenuItem> items;
    switch (menu) {
        case MenuBarMenu::File:
            // Nouveau et Ouvrir n ont pas de raccourci clavier dans
            // l application : la colonne reste vide plutot que d annoncer un
            // Ctrl+N qui ne ferait rien.
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionNew), "", kActionNew, true, false});
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionOpen), "", kActionOpen, true, false});
            items.push_back(MenuItem::make_separator());
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionSave), "Ctrl+S", kActionSave, true, false});
            items.push_back(
                MenuItem{i18n::tr(i18n::StringId::ActionSaveAs), "Ctrl+Maj+S", kActionSaveAs, true, false});
            items.push_back(MenuItem::make_separator());
            items.push_back(
                MenuItem{i18n::tr(i18n::StringId::ActionHome), "Échap", kActionHome, true, false});
            break;

        case MenuBarMenu::Edit: {
            // Memes regles de grisage que le menu contextuel : une entree qui
            // ne ferait rien est grisee, pas active et muette.
            const bool has_selection = editor_.has_selection();
            items.push_back(
                MenuItem{i18n::tr(i18n::StringId::ActionUndo), "Ctrl+Z", kActionUndo, editor_.can_undo(), false});
            items.push_back(
                MenuItem{i18n::tr(i18n::StringId::ActionRedo), "Ctrl+Y", kActionRedo, editor_.can_redo(), false});
            items.push_back(MenuItem::make_separator());
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionCut), "Ctrl+X", kActionCut, has_selection, false});
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionCopy), "Ctrl+C", kActionCopy, has_selection, false});
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionPaste), "Ctrl+V", kActionPaste, true, false});
            items.push_back(MenuItem::make_separator());
            items.push_back(
                MenuItem{i18n::tr(i18n::StringId::ActionSelectAll), "Ctrl+A", kActionSelectAll, true, false});
            break;
        }

        case MenuBarMenu::View:
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionToggleDark), mark(settings_.dark_mode()),
                                     kActionToggleDark, true, false});
            items.push_back(MenuItem{i18n::tr(i18n::StringId::ActionToggleKeyboard), mark(settings_.keyboard_first()),
                                     kActionToggleKeyboard, true, false});
            break;

        case MenuBarMenu::None:
            return;
    }

    int x = 0;
    int y = 0;
    menu_bar_.anchor_for(menu, x, y);
    context_menu_.open(std::move(items), x, y, framebuffer_.width, framebuffer_.height,
                       fonts_.font(Weight::Regular), fonts_.atlas(Weight::Regular, kUiPixelSize));
    menu_bar_.set_open(menu);
    window_.request_repaint();
}

// --- Mise en forme ----------------------------------------------------------

FormatState App::format_state() const {
    FormatState state;
    const doc::CharStyle style = editor_.current_char_style();
    state.bold = style.bold;
    state.italic = style.italic;
    state.underline = style.underline;
    state.heading = editor_.current_heading();
    state.list = editor_.current_list();
    state.align = editor_.current_alignment();

    // Nom montre par le selecteur : celui de la plage sous le curseur, sinon
    // la police par defaut du document, sinon la premiere des reglages (Inter).
    state.font_name = style.font;
    if (state.font_name.empty()) state.font_name = editor_.document().default_font();
    if (state.font_name.empty() && !settings_.fonts().empty()) {
        state.font_name = settings_.fonts().front().display_name;
    }
    return state;
}

void App::open_font_menu() {
    const FormatState state = format_state();

    std::vector<MenuItem> items;
    for (std::size_t i = 0; i < settings_.fonts().size(); ++i) {
        const FontEntry& entry = settings_.fonts()[i];
        // La police courante est signalee par une coche dans la colonne du
        // raccourci : le menu n a pas de colonne d etat, et en ajouter une pour
        // un seul usage ne se justifie pas.
        const bool current = entry.display_name == state.font_name;
        items.push_back(MenuItem{entry.display_name, current ? "•" : "",
                                 static_cast<int>(kActionFontBase + i), true, false});
    }
    if (items.empty()) return;

    int x = 0;
    int y = 0;
    format_bar_.font_picker_anchor(x, y);
    context_menu_.open(std::move(items), x, y, framebuffer_.width, framebuffer_.height,
                       fonts_.font(Weight::Regular), fonts_.atlas(Weight::Regular, kUiPixelSize));
    window_.request_repaint();
}

void App::run_format_action(FormatAction action) {
    switch (action) {
        case FormatAction::Bold: editor_.toggle_bold(); break;
        case FormatAction::Italic: editor_.toggle_italic(); break;
        case FormatAction::Underline: editor_.toggle_underline(); break;

        // Les niveaux de titre et les listes sont des BASCULES : recliquer sur
        // le niveau deja actif remet le paragraphe en corps de texte. Sans
        // cela, il n y aurait aucun moyen de defaire un titre — il faudrait un
        // bouton « corps de texte » de plus, qui ne servirait qu a ca.
        case FormatAction::Heading1:
            editor_.set_heading(editor_.current_heading() == doc::HeadingLevel::H1
                                    ? doc::HeadingLevel::None
                                    : doc::HeadingLevel::H1);
            break;
        case FormatAction::Heading2:
            editor_.set_heading(editor_.current_heading() == doc::HeadingLevel::H2
                                    ? doc::HeadingLevel::None
                                    : doc::HeadingLevel::H2);
            break;
        case FormatAction::Heading3:
            editor_.set_heading(editor_.current_heading() == doc::HeadingLevel::H3
                                    ? doc::HeadingLevel::None
                                    : doc::HeadingLevel::H3);
            break;

        case FormatAction::ListBullet:
            editor_.set_list(editor_.current_list() == doc::ListKind::Bullet
                                 ? doc::ListKind::None
                                 : doc::ListKind::Bullet);
            break;
        case FormatAction::ListNumbered:
            editor_.set_list(editor_.current_list() == doc::ListKind::Numbered
                                 ? doc::ListKind::None
                                 : doc::ListKind::Numbered);
            break;

        // L alignement est exclusif : trois etats, pas trois bascules. Cliquer
        // sur « centre » quand on est deja centre ne doit pas revenir a gauche.
        case FormatAction::AlignLeft: editor_.set_alignment(doc::Alignment::Left); break;
        case FormatAction::AlignCenter: editor_.set_alignment(doc::Alignment::Center); break;
        case FormatAction::AlignRight: editor_.set_alignment(doc::Alignment::Right); break;

        case FormatAction::FontPicker:
            open_font_menu();
            return; // le menu redessine lui-meme ; rien n a change dans le document

        case FormatAction::None:
            return;
    }

    // Meme sortie que n importe quelle mutation : remise en page, curseur
    // visible, titre a jour, repaint. Une bascule de style change la largeur
    // des glyphes, donc la remise en page n est pas facultative.
    after_edit();
}

// --- Parametres ------------------------------------------------------------

void App::run_settings_action(SettingsAction action) {
    switch (action) {
        case SettingsAction::Back:
            show_home();
            return;

        case SettingsAction::ToggleOpenLastNote:
            settings_.set_open_last_note(!settings_.open_last_note());
            platform::log("Ouvrir derniere note : %s",
                          settings_.open_last_note() ? "active" : "desactive");
            break;

        case SettingsAction::ToggleQuoteOfTheDay:
            settings_.set_show_quote_of_the_day(!settings_.show_quote_of_the_day());
            platform::log("Citation du jour : %s",
                          settings_.show_quote_of_the_day() ? "active" : "desactive");
            break;

        case SettingsAction::SetQuoteDurationLoading:
            settings_.set_quote_duration(Settings::QuoteDuration::WhileLoading);
            platform::log("Duree citation : chargement seul");
            break;

        case SettingsAction::SetQuoteDuration5s:
            settings_.set_quote_duration(Settings::QuoteDuration::FiveSeconds);
            platform::log("Duree citation : 5s");
            break;

        case SettingsAction::SetQuoteDuration15s:
            settings_.set_quote_duration(Settings::QuoteDuration::FifteenSeconds);
            platform::log("Duree citation : 15s");
            break;

        case SettingsAction::ToggleDarkMode:
            settings_.set_dark_mode(!settings_.dark_mode());
            platform::log("Mode sombre : %s", settings_.dark_mode() ? "active" : "desactive");
            break;

        case SettingsAction::ToggleKeyboardFirst:
            settings_.set_keyboard_first(!settings_.keyboard_first());
            platform::log("Mode clavier : %s",
                          settings_.keyboard_first() ? "active" : "desactive");
            break;

        case SettingsAction::ToggleSmoothCaret:
            settings_.set_smooth_caret(!settings_.smooth_caret());
            platform::log("Curseur fluide : %s",
                          settings_.smooth_caret() ? "actif" : "inactif");
            break;

        case SettingsAction::PrevLanguage: {
            int current = static_cast<int>(settings_.language());
            current = (current - 1 + static_cast<int>(i18n::Language::Count)) % static_cast<int>(i18n::Language::Count);
            settings_.set_language(static_cast<i18n::Language>(current));
            update_status_line();
            update_window_title();
            break;
        }

        case SettingsAction::NextLanguage: {
            int current = static_cast<int>(settings_.language());
            current = (current + 1) % static_cast<int>(i18n::Language::Count);
            settings_.set_language(static_cast<i18n::Language>(current));
            update_status_line();
            update_window_title();
            break;
        }

        case SettingsAction::FontSizeDown:
            settings_.set_default_font_size(settings_.default_font_size() - 1);
            break;

        case SettingsAction::FontSizeUp:
            settings_.set_default_font_size(settings_.default_font_size() + 1);
            break;

        case SettingsAction::ImportFont:
            import_font_via_dialog();
            sync_registered_fonts();
            break;

        case SettingsAction::RemoveFont: {
            const int index = settings_screen_.selected_font();
            if (index >= 0) {
                const std::string name = settings_.fonts()[static_cast<std::size_t>(index)].display_name;
                if (settings_.remove_font(static_cast<std::size_t>(index), custom_fonts_dir())) {
                    settings_screen_.set_notice("Police retirée : " + name);
                    platform::log("Police retiree : %s", name.c_str());
                    // Le fichier vient d etre supprime : le moteur de rendu ne
                    // doit plus s y referer. Un document qui nommait cette
                    // police retombe sur Inter, sans perdre le nom.
                    sync_registered_fonts();
                }
            }
            break;
        }

        case SettingsAction::None:
            return;
    }

    // Toute action autre que « retour » modifie la mise en page de cet ecran :
    // la liste des polices change de longueur, la bascule change d etat, et le
    // theme change toutes les couleurs. Un relayout complet coute quelques
    // mesures de texte et evite d avoir a savoir laquelle de ces trois choses
    // vient de bouger.
    relayout();
    window_.request_repaint();
}

void App::import_font_via_dialog() {
    std::string path;
    if (!platform::prompt_open_font(window_.handle(), path)) {
        platform::log("Import de police annule par lutilisateur");
        return;
    }

    std::string error;
    if (!settings_.import_font(path, custom_fonts_dir(), error)) {
        // Un import refuse doit dire pourquoi. Le parser rend un message
        // precis (OTTO, .ttc, cmap non geree, fichier tronque) : on le montre
        // tel quel plutot que de le remplacer par un « echec » generique.
        platform::log_error("Import de police refuse : %s", error.c_str());
        settings_screen_.set_notice("Import impossible : " + error);
        return;
    }
    settings_screen_.set_notice("Police importée : " + settings_.fonts().back().display_name);
}

// --- Rendu -----------------------------------------------------------------

void App::draw_editor() {
    const int inset = frame_inset();

    // Le curseur ne clignote que quand il ny a pas de selection : pendant une
    // selection, un curseur qui apparait et disparait a une extremite ajoute
    // du mouvement sans rien apprendre.
    const bool caret = caret_visible_ || editor_.has_selection();
    editor_view_.draw(framebuffer_, editor_, fonts_, body_size(), caret);

    // La barre est dessinee APRES le texte : la zone de texte commence sous
    // elle, mais une ligne de titre plus haute que prevu peut deborder d un ou
    // deux pixels vers le haut, et la barre doit alors la recouvrir.
    if (menu_bar_visible()) menu_bar_.draw(framebuffer_, fonts_);
    if (format_bar_visible()) format_bar_.draw(framebuffer_, fonts_, format_state());

    if (!editor_notice_.empty()) {
        render::draw_utf8(framebuffer_, editor_notice_, kEditorMarginX + inset,
                          framebuffer_.height - 46 - inset, fonts_.font(Weight::Regular),
                          fonts_.atlas(Weight::Regular, kUiPixelSize), theme().subtle);
    }
}

void App::render_frame() {
    framebuffer_.clear(theme().background);

    if (screen_ == Screen::Home) {
        home_.draw(framebuffer_, fonts_, recent_.entries());
    } else if (screen_ == Screen::Settings) {
        settings_screen_.draw(framebuffer_, fonts_, settings_);
    } else {
        draw_editor();
    }

    const int inset = frame_inset();
    render::draw_utf8(framebuffer_, status_line_, 24 + inset,
                      framebuffer_.height - 22 - inset, fonts_.font(Weight::Regular),
                      fonts_.atlas(Weight::Regular, kUiPixelSize), theme().subtle);

    // --- Chrome, toujours par-dessus le contenu ---
    title_bar_.draw(framebuffer_, display_title(), fonts_.font(Weight::Regular),
                    fonts_.atlas(Weight::Regular, kUiPixelSize), window_.is_maximized());

    // Filet de bordure : sans cadre systeme, une fenetre blanche sur fond clair
    // naurait aucun contour. Inutile une fois maximisee.
    if (inset > 0) {
        render::stroke_rect(framebuffer_, 0, 0, framebuffer_.width, framebuffer_.height, 1,
                            theme().foreground);
    }

    context_menu_.draw(framebuffer_, fonts_.font(Weight::Regular),
                       fonts_.atlas(Weight::Regular, kUiPixelSize));
}

} // namespace gw::app
