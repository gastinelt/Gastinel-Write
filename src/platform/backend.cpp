// platform/backend.cpp — selection du backend de presentation au lancement.
#include "platform/backend.h"
#include "platform/log.h"
#include "platform/system.h"

namespace gw::platform {

// Fabrique definie dans gl_backend.cpp. create_gdi_backend est declaree dans
// backend.h : lecran de chargement lutilise directement.
std::unique_ptr<IPresentBackend> create_gl_backend();

const char* backend_name(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL: return "OpenGL (WGL)";
        case BackendKind::Gdi: return "GDI (rendu logiciel)";
    }
    return "inconnu";
}

namespace {

// Force le repli GDI sans toucher au code. Indispensable pour tester ce chemin
// sur une machine qui a un pilote OpenGL parfaitement fonctionnel — sinon le
// repli ne serait jamais exerce avant le jour ou il compte vraiment.
bool gdi_forced_by_environment() {
    const std::string value = environment_variable("GASTINEL_FORCE_GDI");
    return !value.empty() && value[0] != '0';
}

} // namespace

std::unique_ptr<IPresentBackend> create_backend(NativeHandle window) {
    if (gdi_forced_by_environment()) {
        log("GASTINEL_FORCE_GDI actif : OpenGL volontairement ignore");
        if (auto forced = create_gdi_backend(); forced && forced->init(window)) {
            log("Backend actif : %s", backend_name(forced->kind()));
            return forced;
        }
        log_error("Repli GDI force mais indisponible");
        return nullptr;
    }

    // On tente OpenGL en premier, systematiquement. Un echec ici n est pas une
    // erreur : c est le cas nominal sur une machine sans pilote 3D, et le
    // programme doit continuer sans intervention.
    if (auto gl = create_gl_backend(); gl && gl->init(window)) {
        log("Backend actif : %s", backend_name(gl->kind()));
        return gl;
    }
    log("OpenGL indisponible, bascule sur le repli GDI");

    if (auto gdi = create_gdi_backend(); gdi && gdi->init(window)) {
        log("Backend actif : %s", backend_name(gdi->kind()));
        return gdi;
    }

    log_error("Aucun backend de presentation disponible : la fenetre ne peut pas etre affichee");
    return nullptr;
}

} // namespace gw::platform
