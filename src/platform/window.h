// platform/window.h — fenetre Win32 sans chrome systeme.
//
// APPROCHE RETENUE : WS_OVERLAPPEDWINDOW conserve, barre de titre supprimee
// via WM_NCCALCSIZE.
//
// Lalternative etait WS_POPUP avec redimensionnement entierement manuel. Elle
// a ete ecartee : en WS_POPUP, Windows ne fournit plus ni Aero Snap, ni le
// glisser-vers-le-bord, ni les animations de minimisation, ni la gestion
// correcte du multi-ecran DPI — il faudrait tout reimplementer, mal. En
// gardant WS_OVERLAPPEDWINDOW (donc WS_THICKFRAME et WS_CAPTION dans le style)
// et en repondant 0 a WM_NCCALCSIZE, la zone client couvre toute la fenetre :
// le chrome disparait visuellement mais le gestionnaire de fenetres continue
// de traiter la fenetre comme une fenetre normale. Snap, double-clic sur la
// barre de titre et redimensionnement restent geres par DefWindowProc a partir
// de ce que WM_NCHITTEST lui repond.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "platform/keys.h"
#include "platform/native_handle.h"

namespace gw::platform {

class Window {
public:
    // --- Callbacks vers la couche applicative ---
    // Aucun ne recoit de HWND : la couche au-dessus ne doit rien savoir de
    // Win32, pour que ce fichier reste le seul a porter le jour dun portage.

    std::function<void(int, int)> on_resize;
    std::function<void()> on_paint;

    // Vrai si le point client est sur la zone draggable de la fausse barre de
    // titre. Les boutons doivent repondre faux, sinon ils deviendraient une
    // poignee de deplacement et ne recevraient jamais de clic.
    std::function<bool(int, int)> on_is_caption;

    std::function<void(int, int)> on_mouse_move;
    std::function<void(int, int)> on_left_down;
    std::function<void(int, int)> on_left_up;
    std::function<void(int, int)> on_right_down;
    std::function<void()> on_mouse_leave;
    // Delta en crans WHEEL_DELTA (120 par cran de molette standard).
    std::function<void(int)> on_mouse_wheel;
    // Double-clic gauche. Separe de on_left_down parce que Windows envoie
    // WM_LBUTTONDBLCLK *a la place* du second WM_LBUTTONDOWN : sans ce
    // rappel, le second clic dune paire rapide serait purement perdu.
    std::function<void(int, int)> on_left_double_click;

    // Ordre clavier (deplacer, effacer, valider) et modificateurs.
    std::function<void(Key, KeyModifiers)> on_key_down;

    // Caractere saisi, en point de code Unicode deja assemble : la couche
    // au-dessus na a connaitre ni UTF-16 ni paires de substitution. Les
    // caracteres de controle (retour arriere, entree, echappement) sont
    // filtres ici et narrivent jamais — ils sont des ordres, pas du texte.
    std::function<void(std::uint32_t)> on_char;

    // Battement du curseur de saisie. Emis a intervalle systeme tant que le
    // clignotement est arme.
    std::function<void()> on_caret_blink;

    // Tick d'animation fluide haute frequence (60 FPS, 16 ms). Actif uniquement
    // pendant la transition du curseur.
    std::function<void()> on_animation_tick;

    bool create(const std::wstring& title, int width, int height);
    void show();
    int run_message_loop();

    // Poignee opaque, a ne transmettre qu a d autres fonctions de platform/.
    NativeHandle handle() const { return hwnd_; }
    int client_width() const { return client_width_; }
    int client_height() const { return client_height_; }
    bool is_maximized() const;
    float dpi_scale() const;

    // Titre lu par la barre des taches et Alt+Tab. A tenir a jour meme si le
    // chrome natif est invisible : ce n est pas le meme titre que celui qu on
    // dessine soi-meme.
    void set_title(const std::string& utf8_title);

    // Relit la taille de la zone client aupres du systeme et notifie
    // on_resize. Utile juste apres la creation : on ne veut pas dependre du
    // fait qu un WM_SIZE ait deja ete distribue.
    void sync_client_size();

    // --- Capture souris ---
    // Sans capture, relacher un bouton hors de la fenetre ne produit aucun
    // WM_LBUTTONUP et laisse le bouton coince en etat enfonce.
    void capture_mouse();
    void release_mouse();
    bool has_mouse_capture() const;

    // --- Commandes declenchees par les boutons du chrome custom ---
    void minimize();
    void toggle_maximize();
    void close();

    void request_repaint();

    // Arme ou desarme le clignotement du curseur de saisie. Le rythme vient du
    // systeme (GetCaretBlinkTime) et non dune constante : cest un reglage
    // daccessibilite, certains utilisateurs le desactivent.
    void set_caret_blinking(bool blinking);
    void set_animation_timer(bool running);

    // Forme du pointeur sur la zone client. Sans cela, le pointeur resterait la
    // fleche par defaut au-dessus dune zone de texte.
    enum class Cursor {
        Arrow,
        Text,
    };
    void set_cursor_shape(Cursor cursor);

    // Epaisseur de la zone de saisie pour le redimensionnement, en pixels.
    // Exposee parce que le chrome doit sen ecarter : un bouton colle au bord
    // superieur serait inatteignable, la bande de resize passant devant.
    int resize_border_thickness() const;

private:
    // Les types Win32 des messages sont ecrits sous leur forme portable :
    // LRESULT et LPARAM sont des entiers de la taille d un pointeur, WPARAM
    // leur equivalent non signe, UINT un entier 32 bits non signe. Les ecrire
    // ainsi evite d imposer <windows.h> a qui inclut cet en-tete ; la
    // correspondance est verifiee par le compilateur au moment du cast de
    // &window_proc en WNDPROC, dans le .cpp.
    static std::intptr_t __stdcall window_proc(NativeHandle hwnd, unsigned msg,
                                              std::uintptr_t wparam, std::intptr_t lparam);
    std::intptr_t handle_message(unsigned msg, std::uintptr_t wparam, std::intptr_t lparam);

    std::intptr_t hit_test(int screen_x, int screen_y) const;
    void apply_squared_corners() const;
    void ensure_mouse_tracking();

    NativeHandle hwnd_ = nullptr;
    int client_width_ = 0;
    int client_height_ = 0;
    bool tracking_mouse_ = false;
    bool caret_blinking_ = false;
    bool caret_wanted_ = false;
    bool is_active_ = true;
    bool is_minimized_ = false;
    bool animating_ = false;
    Cursor cursor_ = Cursor::Arrow;

    // Moitie haute dune paire de substitution UTF-16, en attente de sa moitie
    // basse. Windows envoie les deux dans deux WM_CHAR consecutifs ; les
    // traiter separement produirait deux caracteres invalides au lieu dun
    // caractere hors du plan multilingue de base.
    std::uint32_t pending_high_surrogate_ = 0;
};

} // namespace gw::platform
