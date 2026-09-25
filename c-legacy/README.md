# Gastinel Write — C ultra-léger

Déclinaison pour machines très anciennes (processeurs/cartes graphiques ère 2000-2009). Pas de système d'extensions. Démarrage de zéro, aucun code de départ.

## Décisions tranchées avec Théo

- **Langage** : C pur, le plus ancien/portable possible ("doit tourner sur n'importe quoi") — pas de sous-ensemble C++, pas de STL, pas d'exceptions.
- **API graphique** : GDI seul (pas de chemin OpenGL en parallèle).

## Encore à trancher avec Théo (non répondu pour l'instant)

- Réutilisation du C++ existant (parser TTF maison, rasterizer, décodeur PNG/inflate) réécrite en C, ou base entièrement neuve et plus simple (ex: police système/bitmap fixe, pas de police custom importable) ?
- Périmètre fonctionnel exact : parité v0 du C++ (`.txt`/`.odt` simple) ou périmètre encore réduit (`.txt` uniquement) ?
- Système cible précis : Windows 2000/XP uniquement, ou descendre jusqu'à Windows 98/95 ? (impacte les API Win32 disponibles)
- Toolchain : MinGW (comme le C++ actuel) ou Open Watcom (plus proche des compilateurs de l'époque) ?
- Nom exact du dossier — actuellement `c-legacy/`, provisoire.

## État actuel (v0)

Éditeur `.txt` fonctionnel, compilé sans avertissement (non testé sur une vraie machine Windows : pas de Windows ici).

- Menus Fichier / Édition / Affichage / ?, raccourcis Ctrl+N/O/S/A, Ctrl+Maj+S.
- Confirmation avant de perdre des modifications, `*` dans le titre si modifié.
- Retour à la ligne activable, fins de ligne Unix converties à l'ouverture, BOM UTF-8 ignoré.
- Ouverture d'un fichier passé en argument (glisser un `.txt` sur l'exe).

Choix provisoires (à valider) : API ANSI uniquement (le texte est en page de code Windows, pas en Unicode — c'est ce qui permet de viser Windows 95) ; police Arial 11 pt ; contrôle EDIT natif.

**Limite connue** : sous Windows 95/98/Me, le contrôle EDIT est plafonné à ~64 Ko de texte (un message l'indique). Sous 2000/XP, pas de limite pratique. Si la cible descend jusqu'à 9x, passer à RichEdit 1.0 (présent dès Windows 95) lèverait cette limite.

## Optimisations

- Aucune dépendance à la bibliothèque C (CRT) : point d'entrée maison, allocations via `HeapAlloc`, fichiers via `CreateFile`.
- Exécutable de **9,7 Ko**, qui n'importe que `kernel32`, `user32`, `gdi32` et `comdlg32` — toutes les fonctions utilisées existent depuis Windows 95.
- Code C89 strict, compilé pour i386 sans SSE : tourne sur tout processeur x86 32 bits.
- En-tête PE marqué sous-système 4.0 pour être accepté par les chargeurs de Windows 95 / NT 4.

## Compiler

```
make        # nécessite i686-w64-mingw32-gcc (paquet gcc-mingw-w64-i686 sous Debian/Ubuntu)
```
