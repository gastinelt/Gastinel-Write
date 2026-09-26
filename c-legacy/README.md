# Gastinel Write — C ultra-léger

Déclinaison pour machines très anciennes (processeurs/cartes graphiques ère 2000-2009). Pas de système d'extensions. Démarrage de zéro, aucun code de départ.

## Décisions tranchées avec Théo

- **Langage** : C pur, le plus ancien/portable possible ("doit tourner sur n'importe quoi") — pas de sous-ensemble C++, pas de STL, pas d'exceptions.
- **API graphique** : GDI seul (pas de chemin OpenGL en parallèle).
- **Système cible** : Windows XP (32 bits) minimum.

## Encore à trancher avec Théo (non répondu pour l'instant)

- Réutilisation du C++ existant (parser TTF maison, rasterizer, décodeur PNG/inflate) réécrite en C, ou base entièrement neuve et plus simple (ex: police système/bitmap fixe, pas de police custom importable) ?
- Périmètre fonctionnel exact : parité v0 du C++ (`.txt`/`.odt` simple) ou périmètre encore réduit (`.txt` uniquement) ?
- Toolchain : MinGW (comme le C++ actuel) ou Open Watcom (plus proche des compilateurs de l'époque) ?
- Nom exact du dossier — actuellement `c-legacy/`, provisoire.

## État actuel (v0)

Éditeur `.txt` fonctionnel, compilé sans avertissement (non testé sur une vraie machine XP : pas de Windows ici).

- Menus Fichier / Édition / Affichage / ?, raccourcis Ctrl+N/O/S/A, Ctrl+Maj+S.
- Confirmation avant de perdre des modifications, `*` dans le titre si modifié.
- Retour à la ligne activable, fins de ligne Unix converties à l'ouverture.
- Ouverture d'un fichier passé en argument (glisser un `.txt` sur l'exe).
- Entièrement en Unicode : les accents et tous les alphabets s'affichent quelle que soit la langue de Windows.
- Lecture UTF-8, UTF-16 (BOM) ou ANSI ; **enregistrement en UTF-8 sans BOM, comme la version Rust/Tauri** : un même fichier s'ouvre dans les deux.
- Écriture atomique (fichier temporaire + `MoveFileEx`) : jamais de document à moitié écrit.
- Styles visuels XP (manifeste), police Tahoma avec ClearType, pas de limite pratique de taille de texte.

Choix provisoires (à valider) : police Tahoma 11 pt ; contrôle EDIT natif ; UTF-8 sans BOM (le Bloc-notes de XP peut mal deviner l'encodage de ces fichiers, mais Gastinel Write les lit correctement).

## Optimisations

- Aucune dépendance à la bibliothèque C (CRT) : point d'entrée maison, allocations via `HeapAlloc`, fichiers via `CreateFile`.
- Exécutable de **12,5 Ko** (manifeste et infos de version inclus), qui n'importe que `kernel32`, `user32`, `gdi32` et `comdlg32`.
- Code C89 strict, compilé pour i386 sans SSE.
- En-tête PE marqué sous-système 5.01, version minimale acceptée par XP.

## Compiler

```
make        # nécessite i686-w64-mingw32-gcc et windres (paquet gcc-mingw-w64-i686 sous Debian/Ubuntu)
```
