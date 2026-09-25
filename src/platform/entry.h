// platform/entry.h — point d entree portable.
//
// Le point d entree reel depend du systeme : wWinMain sous Windows, main
// ailleurs. platform/entry.cpp fournit celui de la plateforme et se contente
// d appeler la fonction ci-dessous, que la couche applicative implemente.
//
// C est un contrat, pas une dependance : platform/ declare la fonction sans
// rien inclure de app/, donc le sens des couches est preserve.
#pragma once

namespace gw {

// Implementee par app/main.cpp. Rend le code de sortie du processus.
int application_main();

} // namespace gw
