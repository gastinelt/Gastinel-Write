# Gastinel Write — C ultra-léger

Déclinaison pour machines très anciennes (processeurs/cartes graphiques ère 2000-2009). Pas de système d'extensions. Démarrage de zéro, aucun code de départ.

## Décisions tranchées avec Théo

- **Langage** : C pur, le plus ancien/portable possible ("doit tourner sur n'importe quoi") — pas de sous-ensemble C++, pas de STL, pas d'exceptions.
- **API graphique** : GDI seul (pas de chemin OpenGL en parallèle).
- **Système cible** : Windows XP (32 bits) minimum.
- **Réutilisation du C++** : oui — les algorithmes maison sont portés en C.
- **Formats** : `.txt` et `.odt`.
- **Toolchain** : MinGW.

## Encore à trancher avec Théo

- Nom exact du dossier — actuellement `c-legacy/`, provisoire.

## État actuel (v0)

Éditeur `.txt` / `.odt` fonctionnel, compilé sans avertissement. **Non testé sur une vraie machine XP** (ni Windows ni Wine disponibles ici) ; les modules de fichiers, eux, sont testés sous Linux (voir plus bas).

- Menus Fichier / Édition / Affichage / ?, raccourcis Ctrl+N/O/S/A, Ctrl+Maj+S.
- Confirmation avant de perdre des modifications, `*` dans le titre si modifié.
- Retour à la ligne activable, ouverture d'un fichier passé en argument (glisser un fichier sur l'exe).
- Entièrement en Unicode ; styles visuels XP (manifeste), Tahoma + ClearType.
- Écriture atomique (fichier temporaire + `MoveFileEx`) : jamais de document à moitié écrit.

### Formats

- **`.txt`** : lecture UTF-8, UTF-16 (BOM) ou ANSI ; écriture UTF-8 sans BOM, comme la version Rust/Tauri.
- **`.odt`** : même sous-ensemble que la version C++. Un `.odt` écrit ici est **identique octet pour octet** à celui qu'écrirait la version C++ pour le même texte sans mise en forme. La lecture accepte les archives compressées (LibreOffice, Word) grâce au décompresseur DEFLATE porté du C++, et comprend aussi titres, tabulations, espaces multiples et sauts de ligne des fichiers LibreOffice simples.
- Cette version n'a **pas de mise en forme** : un `.odt` formaté par la version C++ s'ouvre en texte seul, et un avertissement s'affiche avant de l'enregistrer (la mise en forme serait perdue).

## Organisation du code

```
src/core.[ch]     tampon extensible, CRC-32, allocateur fourni par l'hote
src/inflate.[ch]  decompression DEFLATE      (porte de src/compress/inflate.cpp)
src/zip.[ch]      lecture/ecriture zip        (porte de src/doc/zip.cpp)
src/odt.[ch]      lecture/ecriture .odt       (porte de src/doc/odt_io.cpp)
src/main.c        fenetre Win32/GDI, fichiers, dialogues
tests/            tests natifs des modules purs (Python + gcc + AddressSanitizer)
```

Les modules `core`, `inflate`, `zip` et `odt` ne dépendent ni de Windows ni de la bibliothèque C : ils se compilent tels quels sous Linux pour les tests.

### Reste à porter du C++

Police Inter embarquée (parser TTF), rasterizer de glyphes et décodeur PNG. Ils supposent de remplacer le contrôle EDIT natif par une vue d'édition dessinée à la main en GDI (portage de `text_edit`, `glyph_atlas`, `editor_view`) — c'est le prochain gros chantier, qui ouvre aussi la porte à la mise en forme.

## Optimisations

- Aucune dépendance à la bibliothèque C (CRT) : point d'entrée maison, allocations via `HeapAlloc`, fichiers via `CreateFile`.
- Exécutable de **23 Ko** (lecture/écriture `.odt`, décompression, manifeste et infos de version inclus), qui n'importe que `kernel32`, `user32`, `gdi32` et `comdlg32`.
- Tables de travail en `.bss` (pas dans le fichier), pas de section de relocation (inutile pour un exe sous XP).
- Code C89 strict, compilé pour i386 sans SSE ; en-tête PE sous-système 5.01.

## Compiler et tester

```
make        # nécessite gcc-mingw-w64-i686 (i686-w64-mingw32-gcc + windres)
make test   # tests des modules : lecture des .odt du C++, compression, écriture
            # identique au C++, 300 fichiers corrompus sans plantage (ASan)
```
