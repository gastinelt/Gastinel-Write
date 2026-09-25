// platform/native_handle.h — poignee de fenetre opaque.
//
// APPROCHE RETENUE : un alias sur void*, converti a l interieur des .cpp de
// platform/ vers le HWND reel.
//
// L alternative etait une declaration anticipee d un type nomme
// (struct NativeWindow;) qu on aurait reinterprete en HWND. Elle a ete
// ecartee : elle demande un reinterpret_cast a chaque franchissement de
// frontiere, dans les deux sens, alors que void* se convertit implicitement
// depuis un pointeur d objet et n exige un cast que dans un seul sens, cote
// implementation. Le gain de typage aurait ete cosmetique — les deux formes
// sont egalement opaques pour l appelant — pour un cout de maintenance reel.
//
// La regle qui compte est ailleurs : aucun en-tete public de platform/
// n inclut <windows.h>, donc aucun fichier de app/ ne le recoit, meme
// transitivement. C est verifie a la compilation (voir README, section
// "Poignee opaque").
#pragma once

namespace gw::platform {

// Poignee de fenetre native. Opaque pour tout ce qui est au-dessus de
// platform/ : sa seule utilisation legitime en dehors de platform/ est d etre
// transmise telle quelle a une autre fonction de platform/.
using NativeHandle = void*;

} // namespace gw::platform
