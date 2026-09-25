// platform/keys.h — codes de touches independants du systeme.
//
// En-tete volontairement sans <windows.h> : il est inclus par la couche
// applicative (le menu contextuel, l editeur, la boucle de l application), qui
// ne doit rien savoir des codes virtuels Win32.
//
// La liste reste reduite a ce qui est reellement utilise. Elle a grandi avec
// le moteur d edition — touches de deplacement, d effacement, et les seules
// lettres qui portent un raccourci. Traduire tout le clavier serait de la
// generalite speculative, et chaque entree non utilisee est une entree non
// testee.
//
// LE TEXTE NE PASSE PAS PAR ICI. Une touche est un ordre (deplacer, effacer,
// valider) ; un caractere saisi est une donnee, qui arrive par un chemin
// distinct (WM_CHAR cote Win32, `Window::on_char`). Les melanger obligerait a
// reconstruire soi-meme la disposition clavier, les touches mortes et les
// combinaisons AltGr — que le systeme sait deja faire.
#pragma once

namespace gw::platform {

enum class Key {
    Unknown,
    Escape,

    // Deplacement.
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,

    // Edition.
    Backspace,
    Delete,
    Enter,
    Tab,

    // Seules les lettres qui portent un raccourci sont traduites.
    A,
    B,
    C,
    I,
    S,
    U,
    V,
    X,
    Y,
    Z,
};

// Etat des modificateurs au moment de la frappe.
//
// Regroupes dans une structure plutot que passes en trois booleens : la
// signature de rappel en devient stable si un quatrieme modificateur apparait.
struct KeyModifiers {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

} // namespace gw::platform
