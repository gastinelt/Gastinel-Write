// platform/popup_window.h — petite fenetre sans chrome, a contenu fige.
//
// Concue pour l ecran de chargement : elle encapsule tout ce que cet usage
// demande a Win32 — enregistrement de classe, centrage sur la zone de travail,
// affichage sans vol de focus, presentation.
//
// Son API publique ne porte aucun type systeme, ce qui permet a la couche
// applicative de composer son ecran sans rien savoir de Win32.
#pragma once

#include <memory>
#include <string>

#include "platform/backend.h"
#include "platform/native_handle.h"
#include "render/bitmap.h"

namespace gw::platform {

class PopupWindow {
public:
    ~PopupWindow();

    // Fenetre sans bordure ni barre de titre, centree sur la zone de travail
    // et non sur l ecran entier : centrer sur l ecran la ferait paraitre trop
    // haute sur une configuration a barre des taches epaisse.
    //
    // Prepare aussi sa presentation, en GDI : le contenu est statique et cette
    // fenetre sert justement a couvrir la creation du contexte OpenGL.
    bool create_centered(const std::string& utf8_title, int width, int height);

    // Affiche sans prendre le focus. Un ecran de chargement ne doit pas voler
    // le clavier a la fenetre que l utilisateur regardait.
    void show_without_activating();

    void present(const render::Framebuffer& fb);

    void close();

    bool is_open() const { return hwnd_ != nullptr; }
    bool dismissed() const { return dismissed_; }
    void notify_dismiss() { dismissed_ = true; }
    void pump_events();

private:
    NativeHandle hwnd_ = nullptr;
    std::unique_ptr<IPresentBackend> backend_;
    bool dismissed_ = false;
};

} // namespace gw::platform
