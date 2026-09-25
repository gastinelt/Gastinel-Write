// platform/backend.h — interface de presentation, commune OpenGL et GDI.
//
// Le contrat est volontairement minuscule : un backend ne sait faire qu une
// chose, afficher un Framebuffer CPU dans une fenetre. Tout le rendu reel
// (rasterization, composition) est en amont et identique dans les deux cas.
//
// Consequence voulue : le choix du backend au lancement ne peut pas changer le
// resultat visuel, seulement le chemin d affichage. Une regression graphique ne
// pourra donc jamais venir d ici.
#pragma once

#include "platform/native_handle.h"
#include "render/bitmap.h"

#include <memory>

namespace gw::platform {

enum class BackendKind {
    OpenGL, // WGL, chemin prefere
    Gdi,    // repli logiciel
};

const char* backend_name(BackendKind kind);

class IPresentBackend {
public:
    virtual ~IPresentBackend() = default;

    // Prepare le backend pour cette fenetre. Retourne false si le chemin n est
    // pas disponible sur cette machine (driver absent, format de pixel refuse).
    virtual bool init(NativeHandle window) = 0;

    // Notifie un changement de taille de la zone client.
    virtual void resize(int width, int height) = 0;

    // Affiche le framebuffer et effectue la presentation (swap ou blit).
    virtual void present(const render::Framebuffer& fb) = 0;

    virtual BackendKind kind() const = 0;
};

// Tente OpenGL, retombe sur GDI si l initialisation echoue.
//
// La detection est faite au *runtime*, jamais au build : un seul binaire doit
// fonctionner sur une machine avec pilote accelere comme sur une machine en
// VM sans OpenGL. Retourne nullptr uniquement si meme GDI echoue, ce qui
// signifie que la fenetre elle-meme est inutilisable.
std::unique_ptr<IPresentBackend> create_backend(NativeHandle window);

// Backend GDI seul, sans tentative OpenGL. Utilise par lecran de chargement :
// celui-ci saffiche avant toute initialisation graphique lourde, et na
// aucune raison de payer la creation dun contexte OpenGL pour un ecran
// statique montre quelques dizaines de millisecondes.
std::unique_ptr<IPresentBackend> create_gdi_backend();

} // namespace gw::platform
