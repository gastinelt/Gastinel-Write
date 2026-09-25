#include "platform/popup_window.h"

#include "platform/log.h"
#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gw::platform {
namespace {

constexpr wchar_t kPopupClass[] = L"GastinelWriteSplash";

// La fenetre interagit au clic ou a la touche pour permettre a l utilisateur
// de passer l ecran de chargement immediatement.
LRESULT CALLBACK popup_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<PopupWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_KEYDOWN) {
            self->notify_dismiss();
        }
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

PopupWindow::~PopupWindow() { close(); }

bool PopupWindow::create_centered(const std::string& utf8_title, int width, int height) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &popup_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPopupClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_error("Popup : RegisterClassExW a echoue (%lu)", GetLastError());
        return false;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    // WS_EX_TOOLWINDOW : absente de la barre des taches et d Alt+Tab.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kPopupClass, to_wide(utf8_title).c_str(), WS_POPUP,
                            x, y, width, height, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log_error("Popup : CreateWindowExW a echoue (%lu)", GetLastError());
        return false;
    }

    backend_ = create_gdi_backend();
    if (!backend_ || !backend_->init(hwnd_)) {
        log_error("Popup : backend GDI indisponible");
        close();
        return false;
    }
    backend_->resize(width, height);
    return true;
}

void PopupWindow::show_without_activating() {
    if (hwnd_ != nullptr) ShowWindow(static_cast<HWND>(hwnd_), SW_SHOWNOACTIVATE);
}

void PopupWindow::present(const render::Framebuffer& fb) {
    if (backend_) backend_->present(fb);
    // Pas de boucle de messages : le contenu est fige. UpdateWindow force
    // simplement le premier affichage.
    if (hwnd_ != nullptr) UpdateWindow(static_cast<HWND>(hwnd_));
}

void PopupWindow::close() {
    backend_.reset();
    if (hwnd_ != nullptr) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

void PopupWindow::pump_events() {
    if (hwnd_ == nullptr) return;
    MSG msg{};
    while (PeekMessageW(&msg, static_cast<HWND>(hwnd_), 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace gw::platform
