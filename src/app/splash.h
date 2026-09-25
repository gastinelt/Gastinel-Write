// app/splash.h — ecran de chargement.
//
// Fenetre sans chrome, centree, affichee pendant l initialisation lourde puis
// detruite. Aucune animation : elle apparait et disparait instantanement, et
// aucun delai artificiel n est ajoute. Si l initialisation est rapide, elle ne
// reste visible que quelques dizaines de millisecondes — c est voulu.
//
// Cette classe ne fait que composer une image ; la fenetre et sa presentation
// appartiennent a platform::PopupWindow.
//
// ORDRE D INITIALISATION — ecart assume par rapport a la specification :
// celle-ci demandait le splash avant le chargement de la police. Or le splash
// affiche du texte rendu par notre propre pipeline : sans police, il n aurait
// rien a dessiner. Le compromis retenu est de charger la police en premier
// (lecture et parsing d un TTF, quelques millisecondes) puis d afficher le
// splash avant tout le reste — fenetre principale, contexte OpenGL, atlas,
// ecran d accueil. La seule alternative aurait ete de faire dessiner le splash
// par le moteur de texte de GDI, donc d introduire un second chemin de rendu
// de texte dans le programme.
#pragma once

#include <string>

#include "app/fonts.h"
#include "app/quotes.h"
#include "platform/popup_window.h"
#include "render/bitmap.h"

namespace gw::app {

class SplashScreen {
public:
    // Cree et affiche immediatement la fenetre. `branding_dir` est le dossier
    // ou chercher le logo.
    // show_quote : si vrai, affiche la citation passee en argument.
    bool show(FontSet& fonts, const std::string& branding_dir, bool show_quote = false,
              const Quote* quote = nullptr);

    void close();

    bool is_open() const { return window_.is_open(); }
    bool dismissed() const { return window_.dismissed(); }
    void pump_events() { window_.pump_events(); }
    const render::Framebuffer& framebuffer() const { return framebuffer_; }

private:
    void render(FontSet& fonts);

    platform::PopupWindow window_;
    render::Framebuffer framebuffer_;
    render::Framebuffer logo_;
    bool has_logo_ = false;
    bool show_quote_ = false;
    std::string quote_text_;
    std::string quote_author_;
    int width_ = 560;
    int height_ = 200;
};

} // namespace gw::app
