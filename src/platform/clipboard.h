// platform/clipboard.h — presse-papiers texte.
//
// Reduit au seul format qui nous concerne : du texte brut. Le presse-papiers
// Windows en porte plusieurs simultanement (RTF, HTML, images) ; publier ou
// lire autre chose que du texte n aurait de sens qu une fois le modele de
// document enrichi, et le faire d avance serait du code non teste.
//
// Les chaines sont en UTF-8 des deux cotes : la conversion vers UTF-16 reste
// enfermee dans le .cpp, comme partout dans platform/.
#pragma once

#include <string>

namespace gw::platform {

// Publie `text` dans le presse-papiers. Faux si le presse-papiers est
// verrouille par un autre processus — cas courant et sans gravite, l appelant
// doit simplement ne rien casser.
bool clipboard_set_text(const std::string& text);

// Lit le presse-papiers. Faux s il ne contient pas de texte.
//
// Les fins de ligne CRLF sont normalisees en LF : le reste du programme ne
// connait que LF, et un CR isole se retrouverait sinon insere dans un
// paragraphe, ou il serait invisible mais bien present dans le fichier.
bool clipboard_get_text(std::string& text_out);

} // namespace gw::platform
