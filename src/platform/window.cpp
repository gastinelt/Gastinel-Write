#include "platform/window.h"

#include "platform/log.h"
#include "platform/system.h"
#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

namespace gw::platform {
namespace {

constexpr wchar_t kWindowClass[] = L"GastinelWriteWindow";

// Conversion de la poignee opaque vers le HWND reel. Elle n existe que dans ce
// fichier : c est ici, et nulle part au-dessus, que la nature Win32 de la
// poignee redevient visible.
inline HWND as_hwnd(NativeHandle handle) { return static_cast<HWND>(handle); }

// Extraction des coordonnees dun LPARAM de message souris. Le cast en short
// est indispensable : les coordonnees peuvent etre negatives (curseur capture
// hors de la fenetre, ou ecran secondaire a gauche du principal), et un simple
// LOWORD les transformerait en grands positifs.
// Traduction des codes virtuels Win32 vers l enum neutre. Tout ce qui n est
// pas explicitement reconnu devient Key::Unknown : la couche applicative n a
// alors rien a decider, ce qui est le comportement voulu.
Key key_from_virtual(WPARAM vk) {
    switch (vk) {
        case VK_ESCAPE: return Key::Escape;

        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;

        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_RETURN: return Key::Enter;
        case VK_TAB: return Key::Tab;

        // Les codes virtuels des lettres sont ceux de l ASCII majuscule, et ne
        // dependent pas de la disposition pour les touches alphabetiques d une
        // disposition latine. C est ce que Windows garantit pour les
        // raccourcis ; le texte saisi, lui, passe par WM_CHAR.
        case 'A': return Key::A;
        case 'B': return Key::B;
        case 'I': return Key::I;
        case 'U': return Key::U;
        case 'C': return Key::C;
        case 'S': return Key::S;
        case 'V': return Key::V;
        case 'X': return Key::X;
        case 'Y': return Key::Y;
        case 'Z': return Key::Z;

        default: return Key::Unknown;
    }
}

// Etat des modificateurs, lu au moment ou le message est traite.
//
// GetKeyState et non GetAsyncKeyState : le premier rend l etat tel qu il etait
// quand le message a ete poste, le second l etat physique *maintenant*. Sur une
// frappe rapide, le second attribuerait un Ctrl relache entre-temps a la
// mauvaise touche.
KeyModifiers current_modifiers() {
    KeyModifiers mods;
    mods.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return mods;
}

// Identifiant du minuteur de clignotement. Une seule fenetre, donc une seule
// valeur constante suffit.
constexpr UINT_PTR kCaretTimerId = 1;
constexpr UINT_PTR kAnimTimerId = 2;

inline int lparam_x(LPARAM lp) { return static_cast<int>(static_cast<short>(LOWORD(lp))); }
inline int lparam_y(LPARAM lp) { return static_cast<int>(static_cast<short>(HIWORD(lp))); }

// Epaisseur de la bande de saisie pour le redimensionnement. On najoute pas
// SM_CXPADDEDBORDER a SM_CXSIZEFRAME comme le fait le chrome natif : la bande
// resultante (~8 px) empieterait sur les boutons de titre.
int compute_resize_border() {
    const int frame = GetSystemMetrics(SM_CXSIZEFRAME);
    return frame > 0 ? frame : 4;
}

// Vrai si la barre des taches est en masquage automatique.
//
// Une fenetre sans chrome qui occupe exactement la zone de travail empeche la
// barre des taches masquee de se reveler : Windows ne la fait ressortir que
// sil reste un pixel libre sur son bord. Cest un piege classique des chromes
// custom, invisible tant quon ne teste pas avec le masquage active.
bool taskbar_is_autohidden() {
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    const UINT state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
    return (state & ABS_AUTOHIDE) != 0;
}

} // namespace

int Window::resize_border_thickness() const { return compute_resize_border(); }

bool Window::is_maximized() const {
    if (hwnd_ == nullptr) return false;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(as_hwnd(hwnd_), &wp)) return false;
    return wp.showCmd == SW_SHOWMAXIMIZED;
}

void Window::sync_client_size() {
    if (hwnd_ == nullptr) return;
    RECT rc{};
    if (!GetClientRect(as_hwnd(hwnd_), &rc)) return;
    client_width_ = rc.right - rc.left;
    client_height_ = rc.bottom - rc.top;
    if (on_resize) on_resize(client_width_, client_height_);
}

void Window::set_title(const std::string& utf8_title) {
    if (hwnd_ != nullptr) SetWindowTextW(as_hwnd(hwnd_), to_wide(utf8_title).c_str());
}

void Window::capture_mouse() {
    if (hwnd_ != nullptr) SetCapture(as_hwnd(hwnd_));
}

void Window::release_mouse() {
    if (has_mouse_capture()) ReleaseCapture();
}

bool Window::has_mouse_capture() const {
    return hwnd_ != nullptr && GetCapture() == as_hwnd(hwnd_);
}

void Window::minimize() { ShowWindow(as_hwnd(hwnd_), SW_MINIMIZE); }

void Window::toggle_maximize() {
    ShowWindow(as_hwnd(hwnd_), is_maximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::close() { PostMessageW(as_hwnd(hwnd_), WM_CLOSE, 0, 0); }

void Window::request_repaint() {
    if (hwnd_ != nullptr) InvalidateRect(as_hwnd(hwnd_), nullptr, FALSE);
}

void Window::set_caret_blinking(bool blinking) {
    caret_wanted_ = blinking;
    if (hwnd_ == nullptr) return;

    if (!blinking || !is_active_ || is_minimized_) {
        if (caret_blinking_) {
            KillTimer(as_hwnd(hwnd_), kCaretTimerId);
            caret_blinking_ = false;
        }
        return;
    }

    if (caret_blinking_) return;

    // GetCaretBlinkTime rend la demi-periode reglee par lutilisateur, et
    // INFINITE quand le clignotement est desactive dans les options
    // daccessibilite. Dans ce cas on narme aucun minuteur : le curseur reste
    // affiche en permanence, ce qui est exactement le comportement demande.
    const UINT period = GetCaretBlinkTime();
    if (period == 0 || period == INFINITE) {
        caret_blinking_ = false;
        return;
    }
    SetTimer(as_hwnd(hwnd_), kCaretTimerId, period, nullptr);
    caret_blinking_ = true;
}

void Window::set_animation_timer(bool running) {
    if (hwnd_ == nullptr) return;
    if (running) {
        if (!animating_ && is_active_ && !is_minimized_) {
            SetTimer(as_hwnd(hwnd_), kAnimTimerId, 16, nullptr);
            animating_ = true;
        }
    } else {
        if (animating_) {
            KillTimer(as_hwnd(hwnd_), kAnimTimerId);
            animating_ = false;
        }
    }
}

void Window::set_cursor_shape(Cursor cursor) {
    if (cursor == cursor_) return;
    cursor_ = cursor;

    // Le changement ne prend effet quau prochain WM_SETCURSOR, que Windows
    // nemet quau mouvement suivant. On force donc une application immediate :
    // sinon la forme ne changerait quapres avoir bouge dun pixel.
    if (hwnd_ != nullptr && GetCapture() != as_hwnd(hwnd_)) {
        SetCursor(LoadCursorW(nullptr, cursor_ == Cursor::Text ? IDC_IBEAM : IDC_ARROW));
    }
}

// Windows 11 arrondit les coins de toutes les fenetres de premier niveau. La
// charte Gastinel impose des angles droits stricts, donc on desactive
// explicitement larrondi.
//
// Lappel est resolu dynamiquement : DwmSetWindowAttribute existe depuis Vista
// mais DWMWA_WINDOW_CORNER_PREFERENCE seulement depuis Windows 11 (build
// 22000). Passer par LoadLibrary evite dajouter dwmapi aux dependances de lien
// et laisse Windows 10 fonctionner sans code conditionnel.
void Window::apply_squared_corners() const {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    constexpr DWORD kCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD kDoNotRound = 1;        // DWMWCP_DONOTROUND

    const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) return;
    const auto set_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (set_attribute != nullptr) {
        DWORD value = kDoNotRound;
        // Echoue silencieusement sur Windows 10 : lattribut ny existe pas.
        set_attribute(as_hwnd(hwnd_), kCornerPreference, &value, sizeof(value));
    }
    FreeLibrary(dwm);
}

void Window::ensure_mouse_tracking() {
    if (tracking_mouse_) return;
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = as_hwnd(hwnd_);
    if (TrackMouseEvent(&tme)) tracking_mouse_ = true;
}

// Hit-test complet : bords de redimensionnement dabord, barre de titre
// ensuite, client par defaut.
std::intptr_t Window::hit_test(int screen_x, int screen_y) const {
    RECT window_rect{};
    GetWindowRect(as_hwnd(hwnd_), &window_rect);

    // Les bords ne sont actifs que sur une fenetre restauree : une fenetre
    // maximisee ne se redimensionne pas par ses bords.
    if (!is_maximized()) {
        const int border = compute_resize_border();
        const bool left = screen_x < window_rect.left + border;
        const bool right = screen_x >= window_rect.right - border;
        const bool top = screen_y < window_rect.top + border;
        const bool bottom = screen_y >= window_rect.bottom - border;

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    POINT client{screen_x, screen_y};
    ScreenToClient(as_hwnd(hwnd_), &client);

    // HTCAPTION suffit a obtenir gratuitement de DefWindowProc : le glisser de
    // fenetre, Aero Snap, le double-clic qui maximise, et le menu systeme au
    // clic droit sur la barre. Rien de tout cela nest reimplemente ici.
    if (on_is_caption && on_is_caption(client.x, client.y)) return HTCAPTION;

    return HTCLIENT;
}

std::intptr_t __stdcall Window::window_proc(NativeHandle opaque, unsigned msg,
                                            std::uintptr_t wparam, std::intptr_t lparam) {
    HWND hwnd = as_hwnd(opaque);
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Window*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) return self->handle_message(msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::intptr_t Window::handle_message(unsigned msg, std::uintptr_t wparam, std::intptr_t lparam) {
    switch (msg) {
        // --- Suppression du chrome systeme ---
        case WM_NCCALCSIZE: {
            if (wparam == FALSE) break; // forme sans parametres : rien a ajuster

            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            RECT& rc = params->rgrc[0];

            if (is_maximized()) {
                // Une fenetre maximisee deborde de la zone de travail de
                // lepaisseur de son cadre. Sans cette correction, le contenu
                // serait rogne sur les quatre cotes.
                const int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                rc.left += fx;
                rc.right -= fx;
                rc.top += fy;
                rc.bottom -= fy;

                // LIMITE : seule une barre des taches ancree en bas est traitee.
                // Detecter le bord reel demande ABM_GETAUTOHIDEBAREX par
                // moniteur ; ce nest pas fait ici.
                if (taskbar_is_autohidden()) rc.bottom -= 1;
            }
            // Fenetre restauree : rgrc[0] est laisse tel quel, donc la zone
            // client couvre exactement le rectangle de la fenetre. Cest ce qui
            // fait disparaitre barre de titre et bordures.
            return 0;
        }

        case WM_NCHITTEST:
            return hit_test(lparam_x(lparam), lparam_y(lparam));

        // Le cadre est entierement dessine par nous : laisser DefWindowProc
        // peindre le sien produirait un liseré au changement de focus.
        case WM_NCACTIVATE:
            return TRUE;

        case WM_ACTIVATE: {
            const bool active = LOWORD(wparam) != WA_INACTIVE;
            is_active_ = active;
            if (!active) {
                if (caret_blinking_) {
                    KillTimer(as_hwnd(hwnd_), kCaretTimerId);
                    caret_blinking_ = false;
                }
                if (animating_) {
                    KillTimer(as_hwnd(hwnd_), kAnimTimerId);
                    animating_ = false;
                }
                trim_working_set();
            } else {
                if (caret_wanted_ && !is_minimized_) {
                    set_caret_blinking(true);
                }
                request_repaint();
            }
            return 0;
        }

        // --- Cycle de vie et rendu ---
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED) {
                is_minimized_ = true;
                if (caret_blinking_) {
                    KillTimer(as_hwnd(hwnd_), kCaretTimerId);
                    caret_blinking_ = false;
                }
                if (animating_) {
                    KillTimer(as_hwnd(hwnd_), kAnimTimerId);
                    animating_ = false;
                }
                trim_working_set();
                return 0;
            }
            is_minimized_ = false;
            client_width_ = LOWORD(lparam);
            client_height_ = HIWORD(lparam);
            if (on_resize) on_resize(client_width_, client_height_);
            if (is_active_ && caret_wanted_) {
                set_caret_blinking(true);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(as_hwnd(hwnd_), &ps);
            if (on_paint) on_paint();
            EndPaint(as_hwnd(hwnd_), &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // le backend repeint toute la zone client a chaque frame

        // --- Souris ---
        case WM_MOUSEMOVE:
            ensure_mouse_tracking();
            if (on_mouse_move) {
                on_mouse_move(lparam_x(lparam), lparam_y(lparam));
            }
            return 0;

        case WM_MOUSEWHEEL:
            // Contrairement aux autres messages souris, lparam porte ici des
            // coordonnees ecran. On ne sen sert pas : la vue defilante est
            // celle qui a le focus, il ny en a quune.
            if (on_mouse_wheel) {
                on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wparam));
            }
            return 0;

        case WM_MOUSELEAVE:
            tracking_mouse_ = false;
            if (on_mouse_leave) on_mouse_leave();
            return 0;

        case WM_LBUTTONDOWN:
            if (on_left_down) {
                on_left_down(lparam_x(lparam), lparam_y(lparam));
            }
            return 0;

        case WM_LBUTTONUP:
            if (on_left_up) {
                on_left_up(lparam_x(lparam), lparam_y(lparam));
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (on_right_down) {
                on_right_down(lparam_x(lparam), lparam_y(lparam));
            }
            return 0;

        // Le menu contextuel est dessine par lapplication : on absorbe le
        // message systeme pour que Windows nen ouvre pas un par-dessus.
        case WM_CONTEXTMENU:
            return 0;

        case WM_LBUTTONDBLCLK:
            if (on_left_double_click) {
                on_left_double_click(lparam_x(lparam), lparam_y(lparam));
            }
            return 0;

        // Sans reponse a ce message, Windows repose la fleche a chaque
        // mouvement et le curseur texte clignoterait entre les deux formes.
        // Seule la zone client est concernee : sur les bords, la fleche de
        // redimensionnement rendue par hit_test doit rester celle du systeme.
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr,
                                      cursor_ == Cursor::Text ? IDC_IBEAM : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
        // Sans WM_SYSKEYDOWN, une combinaison avec Alt ne serait jamais vue.
        // On la traduit quand meme pour que la couche au-dessus puisse decider
        // de l ignorer explicitement.
        case WM_SYSKEYDOWN:
            if (on_key_down) on_key_down(key_from_virtual(wparam), current_modifiers());
            return 0;

        // --- Texte saisi ---
        //
        // Chemin distinct de WM_KEYDOWN a dessein : ici, Windows a deja
        // applique la disposition clavier, les touches mortes et AltGr. Le
        // reconstruire a partir des codes virtuels serait refaire ce travail,
        // moins bien, et casserait toute disposition non francaise.
        case WM_CHAR: {
            const auto unit = static_cast<std::uint32_t>(wparam);

            // Paire de substitution UTF-16 : deux messages pour un caractere.
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                pending_high_surrogate_ = unit;
                return 0;
            }
            std::uint32_t codepoint = unit;
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                if (pending_high_surrogate_ == 0) return 0; // moitie basse orpheline
                codepoint = 0x10000 + ((pending_high_surrogate_ - 0xD800) << 10) +
                            (unit - 0xDC00);
                pending_high_surrogate_ = 0;
            } else {
                pending_high_surrogate_ = 0;
            }

            // Les caracteres de controle arrivent aussi par WM_CHAR (Entree en
            // 0x0D, Retour arriere en 0x08, Echap en 0x1B). Ce sont des ordres,
            // deja traites par WM_KEYDOWN : les laisser passer ici les
            // insererait une seconde fois, en tant que texte.
            if (codepoint < 0x20 || codepoint == 0x7F) return 0;

            if (on_char) on_char(codepoint);
            return 0;
        }

        case WM_TIMER:
            if (wparam == kCaretTimerId && on_caret_blink) on_caret_blink();
            else if (wparam == kAnimTimerId && on_animation_tick) on_animation_tick();
            return 0;

        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<const RECT*>(lparam);
            if (rect) {
                SetWindowPos(as_hwnd(hwnd_), nullptr, rect->left, rect->top,
                             rect->right - rect->left, rect->bottom - rect->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(as_hwnd(hwnd_), msg, wparam, lparam);
}

bool Window::create(const std::wstring& title, int width, int height) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    // CS_OWNDC : requis par WGL. CS_DBLCLKS : sans lui, Windows nenvoie
    // jamais WM_LBUTTONDBLCLK et la selection dun mot au double-clic serait
    // impossible a distinguer de deux clics simples.
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    // Le cast est le seul point ou la signature portable de window_proc est
    // reconciliee avec WNDPROC. Les types sous-jacents sont identiques ; seule
    // la poignee change de nom.
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Window::window_proc);
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_error("RegisterClassExW a echoue (code %lu)", GetLastError());
        return false;
    }

    // WS_OVERLAPPEDWINDOW est conserve entier. WS_THICKFRAME et WS_CAPTION
    // sont ce qui donne acces au snap et au redimensionnement natifs ; cest
    // WM_NCCALCSIZE qui les rend invisibles, pas labsence de style.
    hwnd_ = CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log_error("CreateWindowExW a echoue (code %lu)", GetLastError());
        return false;
    }

    apply_squared_corners();

    // Force un recalcul de la zone non cliente : sans cela, la premiere frame
    // conserverait le cadre calcule avant que notre WM_NCCALCSIZE ne sapplique.
    SetWindowPos(as_hwnd(hwnd_), nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void Window::show() {
    ShowWindow(as_hwnd(hwnd_), SW_SHOW);
    UpdateWindow(as_hwnd(hwnd_));
}

int Window::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

float Window::dpi_scale() const {
    if (hwnd_ == nullptr) return 1.0f;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *GetDpiForWindowProc)(HWND);
        auto get_dpi = reinterpret_cast<GetDpiForWindowProc>(GetProcAddress(user32, "GetDpiForWindow"));
        if (get_dpi) {
            UINT dpi = get_dpi(as_hwnd(hwnd_));
            if (dpi > 0) return static_cast<float>(dpi) / 96.0f;
        }
    }
    HDC hdc = GetDC(as_hwnd(hwnd_));
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(as_hwnd(hwnd_), hdc);
        if (dpi > 0) return static_cast<float>(dpi) / 96.0f;
    }
    return 1.0f;
}

} // namespace gw::platform
