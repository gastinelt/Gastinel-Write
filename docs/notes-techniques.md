# Notes techniques — Gastinel Write

Journal des décisions d'architecture, des mesures et des compromis. Pour la
présentation du projet, l'état d'avancement et le résumé des instructions de
compilation, voir le [README](../README.md).

Ce document est tenu au fil du développement : chaque section explique un choix
et ce qu'il coûte, y compris quand le choix est de ne rien faire.

---

## Compilation

Cible : Windows 10 / 11, x86-64. Testé avec MinGW-w64 g++ 13.2 (UCRT) et CMake
4.4, sans aucun avertissement. MSVC est prévu dans le `CMakeLists.txt` mais n'a
pas été exercé.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Sept cibles, dont six outils console qui ne créent aucune fenêtre :

| Cible | Rôle |
|---|---|
| `gastinel_write` | l'application |
| `gw_edit_tests` | cœur du moteur d'édition : curseur, sélection, annulation |
| `gw_style_tests` | modèle de document enrichi : plages de style, mise en forme de paragraphe, syntaxe Markdown en direct |
| `gw_doc_roundtrip` | E/S documents (`.txt`, `.odt`) |
| `gw_compress_tests` | décodeur DEFLATE et PNG maison, contre références externes |
| `gw_settings_tests` | réglages et import de police (voir *Paramètres*) |
| `gw_screen_render` | compose les écrans en BMP, sans fenêtre (accueil, paramètres, éditeur, mise en forme, mode clavier, menu ouvert) |

```bash
python tools/gen_external_odt.py /un/dossier
./build/gw_edit_tests.exe
./build/gw_style_tests.exe
./build/gw_doc_roundtrip.exe /chemin/vers/un/dossier/de/travail
python tools/gen_compress_vectors.py build/vectors
./build/gw_compress_tests.exe build/vectors assets/branding
./build/gw_settings_tests.exe assets/fonts/Inter-Regular.ttf /un/dossier
./build/gw_screen_render.exe assets /un/dossier
```

Les deux scripts Python produisent des vecteurs de test à partir d'une
implémentation de référence indépendante (le module `zlib`) ; sans eux, les
tests concernés s'annoncent `[SKIP]` ou échouent (voir *Décompression*).

### Binaire autonome

Le runtime de la chaîne de compilation est lié statiquement
(`-static-libgcc -static-libstdc++ -static`) : le binaire n'importe plus
`libstdc++-6.dll`, `libgcc_s_seh-1.dll` ni `libwinpthread-1.dll`, et se copie
donc tel quel sur une machine sans MinGW. Vérifié par `objdump -p` : il ne reste
que des DLL du système.

Le compromis est le poids — en `Release`, **924 Kio → 1 377 Kio** (+49 %).
Il est accepté : une dépendance à trois DLL introuvables hors machine de
développement coûte plus cher que 453 Kio.

Cela ne touche pas les `api-ms-win-crt-*.dll` (UCRT), qui appartiennent au
système et non à la toolchain — le plancher Windows reste celui décrit dans
*Préparation portage*.

### Interrupteurs de développement

Ni l'un ni l'autre n'a d'effet en usage normal. Ils existent parce que les deux
comportements concernés sont sinon impossibles à observer sur une machine de
développement.

| Variable | Effet |
|---|---|
| `GASTINEL_FORCE_GDI=1` | Ignore OpenGL et force le repli GDI. Sans ça, ce chemin ne serait jamais exercé sur une machine avec pilote 3D fonctionnel. |
| `GASTINEL_SPLASH_HOLD_MS=3000` | Maintient l'écran de chargement à l'écran. Il ne vit normalement que quelques dizaines de millisecondes. **Aucun délai n'est ajouté par défaut.** |

---

## Architecture

| Dossier | Rôle |
|---|---|
| `src/platform/` | Win32 : fenêtre, boucle de messages, contexte WGL, surface GDI, décodage d'image, dialogues système, logs. Seul endroit qui inclut `windows.h` : aucun en-tête public de `platform/` ne le laisse fuir vers `app/`. |
| `src/font/` | Parser TTF maison. Ne connaît ni pixels ni Windows : il ne produit que des contours en unités de police. |
| `src/render/` | Rasterizer scanline, atlas de glyphes, primitives de composition, contours des icônes. Aucune API graphique ici. |
| `src/doc/` | Modèle de document et sa mise en forme, moteur d'édition, reconnaissance de syntaxe Markdown, E/S `.txt` et `.odt`, zip minimal. Ne dépend ni de Win32 ni du moteur de rendu. |
| `src/compress/` | Inflate DEFLATE (RFC 1951), enveloppe zlib (RFC 1950), CRC-32 et Adler-32. Module pur. |
| `src/image/` | Décodeur PNG. Module pur, sans aucun codec système. |
| `src/app/` | Assemblage : écrans, chrome, barre de menu, barre de mise en forme, menu contextuel, jeu de polices, notes récentes. |
| `tools/` | Harnais de vérification hors interface et profileur mémoire. |

### Le point d'architecture central

Tout le rendu est **CPU**. Les backends OpenGL et GDI ne savent faire qu'une
chose : afficher un `Framebuffer` BGRA déjà composé.

```
TTF → contours → rasterizer → atlas 8 bits → framebuffer BGRA → [ OpenGL | GDI ]
                                                                 └─ présentation seule
```

Conséquence voulue : **le choix du backend ne peut pas changer un seul pixel.**
Une régression visuelle ne pourra jamais venir du chemin de présentation. C'est
ce qui rend le repli GDI sûr — ce n'est pas un mode dégradé, c'est le même
rendu affiché autrement, plus lentement.

Le corollaire est appliqué partout : les icônes du chrome et de l'accueil ne
sont ni des bitmaps ni des primitives de dessin dédiées, ce sont **des contours
fermés passés au même rasterizer que le texte** (`render/icons.cpp`). Un seul
chemin de rasterization dans tout le programme.

### Poignée de fenêtre opaque

`app/` ne connaît **aucun type Win32**, et ce n'est plus une convention : c'est
vérifié par le compilateur. Aucun en-tête public de `platform/` n'inclut
`<windows.h>`, donc aucun fichier de `app/` ne le reçoit, même transitivement.

La poignée circule sous la forme d'un alias opaque déclaré dans
`platform/native_handle.h` :

```cpp
using NativeHandle = void*; // converti en HWND uniquement dans les .cpp de platform/
```

L'alternative — une déclaration anticipée d'un type nommé (`struct NativeWindow;`)
réinterprétée en `HWND` — a été écartée : elle impose un `reinterpret_cast` dans
les deux sens à chaque franchissement de frontière, pour un gain de typage
cosmétique (les deux formes sont également opaques pour l'appelant).

Les signatures qui portaient des types de messages Win32 (`LRESULT`, `WPARAM`,
`LPARAM`, `POINT`) sont écrites sous leur forme portable : `std::intptr_t`,
`std::uintptr_t`, `unsigned`, deux `int`. La correspondance est vérifiée par le
compilateur au seul endroit où elle compte, le `reinterpret_cast<WNDPROC>` de
`window.cpp`.

**Vérification** — le test n'est pas un `grep` sur les `#include` écrits à la
main, qui ne dirait rien des dépendances transitives. On empoisonne
`<windows.h>` et on laisse le préprocesseur trancher :

```bash
mkdir -p /tmp/poison && echo '#error "app/ ne doit pas inclure <windows.h>"' > /tmp/poison/windows.h
for f in src/app/*.cpp; do c++ -std=c++20 -fsyntax-only -DUNICODE -D_UNICODE -DNOMINMAX -I/tmp/poison -Isrc "$f"; done
```

Les quatorze fichiers de `app/` passent sans un diagnostic. La bande de bruit
habituelle d'un `grep` (commentaires mentionnant `HWND`, en-têtes tiers) ne
peut pas fausser ce résultat.

---

## Détection du backend

La détection est faite **au runtime, pas au build** : un seul binaire doit
tourner sur une machine avec pilote 3D comme sur une VM sans OpenGL.

1. tentative `ChoosePixelFormat` / `wglCreateContext` / `wglMakeCurrent` ;
2. en cas d'échec, log explicite et bascule sur GDI (`CreateDIBSection` + `BitBlt`) ;
3. le backend actif est écrit dans le journal **et** affiché dans la ligne d'état.

Un échec OpenGL n'est pas traité comme une erreur : c'est le cas nominal sur
une machine sans pilote.

### Contraintes assumées côté OpenGL

Le backend GL suppose au minimum le sous-ensemble **OpenGL 1.1** garanti par
`opengl32.dll` : pas de shader, pas de VBO, pas de chargeur d'extensions. Deux
capacités au-delà de 1.1 sont détectées au runtime et utilisées si présentes,
avec repli complet sinon :

- `GL_BGRA` (OpenGL 1.2) — sinon conversion BGRA→RGBA sur CPU ;
- textures non puissance de deux (OpenGL 2.0, ou l'extension `GL_ARB_texture_non_power_of_two`)
  — sinon arrondi à la puissance de deux englobante. Cf. *Profil mémoire* :
  ce seul repli coûtait 8,5 Mo sur une fenêtre maximisée.

---

## Chrome de fenêtre custom

### Approche retenue : `WS_OVERLAPPEDWINDOW` + `WM_NCCALCSIZE`

La fenêtre garde **tous** ses styles natifs (`WS_THICKFRAME`, `WS_CAPTION`) ;
c'est `WM_NCCALCSIZE` répondant `0` qui étend la zone client à toute la fenêtre
et fait disparaître le chrome système.

L'alternative `WS_POPUP` a été écartée : elle supprime aussi Aero Snap, le
glisser-vers-le-bord, les animations de minimisation et la gestion multi-écran
DPI, qu'il faudrait tout réimplémenter. Ici, `WM_NCHITTEST` renvoie `HTCAPTION`
sur la fausse barre de titre et `HTLEFT`/`HTTOPRIGHT`/… sur les bords, et
`DefWindowProc` fournit gratuitement le reste.

### Ce qui fonctionne, vérifié

| Comportement | Mesure |
|---|---|
| Glisser la fenêtre par la barre de titre | ✅ |
| Aero Snap (bord gauche) | ✅ fenêtre 960×1028 à (0,0), soit exactement la moitié gauche de la zone de travail |
| Maximiser / restaurer (bouton) | ✅ client = 1920×1028 à (0,0) = zone de travail exacte ; retour à 900×620 |
| Double-clic sur la barre de titre | ✅ (fourni par `DefWindowProc` via `HTCAPTION`) |
| Redimensionnement par un bord | ✅ tirage de 100 px sur le bord gauche → gauche −100, largeur +100, exact |
| Redimensionnement par un coin | ✅ |
| Minimiser, fermer | ✅ |
| Coins carrés sous Windows 11 | ✅ `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND`, résolu dynamiquement (aucun lien vers `dwmapi`, dégradation silencieuse sous Win10) |

Les boutons sont dessinés à la main, sans `HWND` enfant, et gérés par hit-test
manuel sur `WM_LBUTTONDOWN`/`WM_LBUTTONUP`, avec capture souris et validation
seulement si le relâchement a lieu sur le bouton enfoncé.

### Limites connues du chrome

- **Snap Layouts de Windows 11 (survol du bouton Agrandir) n'apparaît pas.**
  Ce menu exige que `WM_NCHITTEST` réponde `HTMAXBUTTON` sur le bouton, ce qui
  ferait passer les clics par les messages *non client*. La spécification
  demandait explicitement une gestion par `WM_LBUTTONDOWN`/`WM_LBUTTONUP` sur
  les rectangles ; les deux sont incompatibles et la spécification l'emporte.
- **Les ~4 px supérieurs des boutons de titre sont une poignée de
  redimensionnement**, la bande de resize passant devant. C'est aussi le
  comportement du chrome natif de Windows.
- **Pas d'ombre portée.** Aucun appel `DwmExtendFrameIntoClientArea`. Un filet
  noir de 1 px délimite la fenêtre à la place — sans lui, une fenêtre blanche
  sans cadre serait invisible sur un fond clair.
- **Barre des tâches en masquage automatique** : seul un ancrage en bas est
  traité (1 px réservé pour que la barre puisse se révéler). Détecter le bord
  réel demanderait `ABM_GETAUTOHIDEBAREX` par moniteur.
- **Pas de gestion du DPI par moniteur.** Les dimensions du chrome sont en
  pixels fixes ; sur un écran à 150 % le chrome sera mis à l'échelle par
  Windows, donc flou.

### Menu contextuel

`app/context_menu.h` est générique : il reçoit une liste d'entrées et dit
laquelle a été choisie, sans rien connaître du métier. Positionnement replié
près des bords, fermeture au clic extérieur et à Échap, survol par inversion
monochrome, apparition instantanée, angles droits, aucune ombre.

**Limite assumée** : le menu est dessiné dans le framebuffer de la fenêtre, il
ne peut donc pas déborder hors de celle-ci. Un menu Win32 réel est une fenêtre
popup de niveau supérieur. Le passage à une fenêtre dédiée ne changerait pas
l'interface de la classe.

Les actions (Couper / Copier / Coller / Sélectionner tout) sont branchées sur
exactement les mêmes fonctions que les raccourcis clavier — le menu ne porte
aucune logique propre. Couper et Copier sont grisés sans sélection : une entrée
active qui ne ferait rien laisserait croire à une panne.

---

## Écran de chargement

Fenêtre `WS_POPUP` centrée sur la zone de travail, sans chrome, **rendue en
GDI seul** — c'est un écran statique dont le rôle est justement de couvrir la
création du contexte OpenGL ; lui en faire créer un serait absurde. Il passe
par `create_gdi_backend()`, donc par le même chemin de composition que tout le
reste. Aucune animation, aucun délai artificiel, fermeture dès que l'accueil
est prêt.

**Écart assumé sur l'ordre d'initialisation.** La spécification demandait le
splash *avant* le chargement de la police. Or le splash affiche du texte en
Inter rendu par notre propre pipeline : sans la police, il n'a rien à dessiner.
L'ordre retenu est donc : police (lecture + parsing d'un TTF, quelques
millisecondes) → splash → tout le reste (fenêtre, contexte OpenGL, atlas,
accueil). La seule alternative aurait été de faire dessiner le splash par le
moteur de texte de GDI, donc d'introduire un second chemin de rendu de texte
dans le programme.

### Logo

Le splash charge `assets/branding/icone_write_w.png`. Si le fichier est absent,
l'emplacement reste vide et une erreur explicite est journalisée — aucun
placeholder n'est inventé.

**Nommage : la lettre désigne le fond de l'icône, pas le fond d'accueil.**
Vérifié en lisant les pixels, pas déduit du nom :

| Fichier | Contenu réel | Luminance moyenne |
|---|---|---|
| `icone_write_b.png` | tuile **noire**, symbole blanc, 477×477 | 29 |
| `icone_write_w.png` | tuile **blanche**, symbole noir, 512×500 | 229 |

C'est l'inverse de ce que décrivait la spécification d'origine (« `_b` = version
sur fond blanc, symbole noir »). Le splash étant blanc et la charte étant
monochrome noir sur blanc, c'est `_w` qu'il faut : elle pose le symbole noir
directement sur le fond, sans pavé. `_b` servira à un écran sombre, s'il en
apparaît un. Le choix est commenté dans `splash.cpp` pour éviter une
« correction » à l'envers plus tard.

**Défaut d'export corrigé.** Une première version de `icone_write_w.png`
(513×513) portait une ligne sombre sur son seul bord bas — `y=511` à 185 et
`y=512` à 14, contre 255 de fond — qui apparaissait comme un filet gris sous le
logo. L'asset a été ré-exporté ; les quatre bords sont désormais à 254-255.

Rien n'avait été rogné dans le code pour masquer ce défaut : le
rééchantillonnage reproduisait fidèlement l'image fournie (l'ancien
plus-proche-voisin ne tombait simplement jamais sur cette dernière ligne et la
cachait par hasard). Corriger un asset précis depuis le moteur de rendu aurait
été une correction au mauvais endroit.

### Redimensionnement de l'image

Le logo est fourni en 512 px et posé en 144, soit une réduction de 3,6×. Le
report d'image (`blit_image`) utilise donc un **filtre boîte** en réduction :
au plus proche voisin, cinq pixels sources sur six étaient jetés et les
diagonales du symbole partaient en escalier. La moyenne est pondérée par
l'alpha, sinon la couleur des pixels transparents remonterait et cernerait le
logo d'un liseré.

`kLogoSide` définit une **boîte** carrée, pas une taille imposée : l'image y est
inscrite en respectant son ratio puis recentrée. L'asset courant n'est pas carré
(512×500) et serait sinon étiré de 2,4 %, ce qui se verrait sur ses diagonales.
La colonne de texte s'aligne sur la boîte et non sur l'image, pour qu'un asset
plus étroit ne déplace pas tout le bloc.

Ce n'est pas un revirement sur la dette « aucun antialiasing » : celle-ci porte
sur le **rasterizer de glyphes**, qui transforme des contours vectoriels en
pixels. Ici on rééchantillonne une image déjà tramée — question distincte. En
agrandissement, le plus proche voisin est conservé : interpoler ne ferait que
ramollir des angles droits, ce que la charte refuse.

Le décodage PNG est **entièrement maison** : `image/png.cpp` par-dessus notre
propre inflate. GDI+ a été retiré du projet une fois l'équivalence prouvée pixel
par pixel. Cf. *Décompression*.

---

## Menu d'accueil

Rendu dans la zone cliente de la fenêtre principale, pas dans une fenêtre
séparée. Aucune animation d'apparition.

- **Icône paramètres** en haut à gauche, discrète : trois curseurs horizontaux,
  volontairement pas un engrenage. Au clic : ouvre l'écran des *Paramètres*.
- **« Créer une nouvelle note »** — fond de page, filet, texte à l'encre.
- **« Ouvrir une note »** — surface inversée (noire en clair, blanche en sombre).
- **Grille** : marges latérales de 17 % de chaque côté, 2 colonnes, 7 lignes
  visibles, défilement vertical à la molette au-delà. Cartes basses (44 px),
  titre à gauche tronqué avec une ellipsis sur une frontière de point de code,
  icône de fichier générique + extension réelle à droite.
- Liste vide : mention discrète « Aucune note récente. »

**Écart sur l'icône de type de fichier.** La spécification demandait
l'extension « dessus/dedans » l'icône. Elle est placée **à droite** de la
forme : à 18 px de haut, du texte inscrit dans l'icône serait illisible sans
antialiasing (cf. dette technique). Le principe est respecté — une seule forme
de base, seul le texte de l'extension change.

### Persistance des notes récentes

`%APPDATA%\Gastinel\Write\recent.txt` — **un chemin absolu par ligne**, UTF-8
sans BOM, CRLF, le plus récent en tête, 40 entrées au maximum. Les lignes vides
et celles commençant par `#` sont ignorées ; un BOM UTF-8 éventuel est retiré à
la lecture.

Pourquoi pas de JSON : il faudrait un parseur, donc soit une dépendance externe
(interdite), soit un parseur maison pour stocker… une liste de chaînes. Un
chemin par ligne est lisible à l'œil, éditable à la main, et ne peut pas se
corrompre partiellement. Un chemin Windows ne peut de toute façon pas contenir
de retour à la ligne, le séparateur est donc sans ambiguïté.

**Limite** : la déduplication compare les chemins de façon sensible à la casse
alors que Windows ne l'est pas. Deux graphies du même fichier créeraient deux
entrées.

### Intégration réelle des deux boutons

- **Créer une nouvelle note** : vide le document, bascule sur l'éditeur.
  Aucun fichier n'est créé ; il n'y a pas encore d'enregistrement.
- **Ouvrir une note** : `GetOpenFileNameW` filtré sur `.txt`/`.odt`, puis
  chargement **réel** via `doc/`, ajout à la liste des récents, bascule sur
  l'éditeur.
- **Clic sur une carte** : même chemin de chargement.

L'éditeur affiche le contenu du document **en lecture seule** : pas de curseur,
pas de saisie, pas de retour à la ligne automatique, pas de défilement. Au-delà
de la hauteur visible, une mention le signale. Échap ramène à l'accueil.

---

## Paramètres

Accessibles par l'icône du coin supérieur gauche de l'accueil. C'est un
troisième écran de la même fenêtre — même chrome custom, aucune boîte de
dialogue système. Échap y ramène à l'accueil, comme depuis l'éditeur.

### Persistance

`%APPDATA%\Gastinel\Write\settings.txt`, à côté de `recent.txt` et dans le même
esprit : texte UTF-8, une entrée par ligne, `clé=valeur`, CRLF, `#` en
commentaire. Pas de parseur, éditable à la main, impossible à corrompre
partiellement.

```
dark_mode=0|1
default_font_size=<8..48>
font=<nom affiché>|<nom de fichier dans assets/fonts/custom>   (répétable)
```

Les deux fichiers sont restés séparés plutôt que fusionnés : ils n'ont ni le
même rythme d'écriture (les récents changent à chaque ouverture de note, les
réglages presque jamais) ni la même conséquence en cas de perte.

### Mode sombre

Toutes les couleurs du programme sont passées dans `app/theme.h`. La règle qui
tient l'ensemble : **un `0xFF......` écrit ailleurs que dans ce fichier est un
bug.** C'est ce qui garantit que la bascule atteint tout l'écran — chrome,
accueil, éditeur, menu contextuel, écran de chargement — et pas seulement les
zones auxquelles on aurait pensé.

Le mode sombre n'introduit aucune couleur : il échange les rôles. Les gris ne
sont pas repris tels quels mais renversés autour du milieu (`0xF2` → `0x14`) ;
les réutiliser sur fond noir donnerait des survols plus lumineux que le texte,
donc une hiérarchie visuelle inversée.

Deux conséquences concrètes :

- les réglages sont chargés **avant** l'écran de chargement, sinon chaque
  démarrage en mode sombre commencerait par un flash blanc ;
- le logo du splash bascule sur la variante `icone_write_b.png`. Le commentaire
  du code disait « la variante `_b` servira à un fond sombre, si un tel écran
  apparaît un jour » — c'est ce jour.

### Vérification : `gw_screen_render`

Le rendu étant entièrement CPU (cf. « Le point d'architecture central »), un
écran complet se compose sans créer la moindre fenêtre. La cible
`gw_screen_render` le fait et écrit le framebuffer en BMP :

```bash
./build/gw_screen_render.exe assets /un/dossier
```

Quatre images : accueil et paramètres, en clair et en sombre. C'est ce qui rend
une modification d'interface vérifiable en regardant des pixels plutôt qu'en
cliquant. Le BMP est choisi parce que le framebuffer est déjà en BGRA — le
projet a un décodeur PNG, pas un encodeur, et en ajouter un pour un outil de
développement serait disproportionné.

### Taille de police par défaut

Corps utilisé par l'éditeur, borné à `[8, 48]`. Il est lu à chaque frame et non
mémorisé : `FontSet` crée l'atlas correspondant à la demande, donc changer la
valeur n'a aucune étape d'invalidation à orchestrer.

### Import de polices

Bouton → `GetOpenFileName` filtré sur `.ttf` → validation par le parser réel →
copie dans `assets/fonts/custom/` → ajout à la liste.

La validation a lieu **avant toute écriture**, et elle passe le fichier au
parser du programme, pas à un contrôle d'extension : c'est le seul test qui
dise si la police sera effectivement affichable par notre pipeline. S'y ajoute
un contrôle que `A`, `a`, `e` ou `0` sort réellement du parser — une police peut
être structurellement valide sans couvrir l'alphabet latin (police d'icônes en
plage privée, sous-ensemble), et l'accepter donnerait une interface vide sans
message.

Le nom du fichier copié est assaini (tout ce qui n'est pas `[A-Za-z0-9_-]`
devient `_`) : un nom venu d'ailleurs peut contenir des séparateurs, donc écrire
hors du dossier. Un second import du même nom est suffixé plutôt qu'écrasé.

La **liste fait foi**, pas le contenu du dossier : un `.ttf` déposé à la main
dans `custom/` n'apparaît pas, et une entrée dont le fichier a disparu est
éliminée au chargement, fichier réécrit. C'est ce qui empêche un fichier jamais
validé de se retrouver proposé à l'utilisateur.

#### Ce que l'import refuse, avec un message

Mesuré par `gw_settings_tests` (voir plus bas), pas supposé :

| Fichier soumis | Message rendu |
|---|---|
| vide, ou plus court qu'un en-tête | `fichier trop court pour etre un TTF` |
| texte renommé en `.ttf` | `signature sfnt inconnue : ce fichier n est pas un TTF` |
| OpenType/CFF (`OTTO`) | `police OpenType/CFF (OTTO) non supportee : il faut un TTF a contours glyf` |
| collection (`ttcf`, `.ttc`) | `collection TrueType (.ttc) : extrayez la police voulue en .ttf` |
| WOFF / WOFF2 | `police web (WOFF/WOFF2) non supportee : il faut un .ttf` |
| police réelle tronquée | `table glyf absente` |

Le message du parser est affiché tel quel dans l'écran des paramètres. Le
remplacer par un « échec » générique ferait perdre la seule information qui
permet à l'utilisateur de savoir quoi faire.

#### Limites connues, assumées

- **`.otf` n'est pas dans le filtre.** Le parser ne lit que des contours `glyf` ;
  un `.otf` porte le plus souvent du CFF, qu'il refuserait. Proposer une
  extension qui échoue neuf fois sur dix serait une fausse fonctionnalité. Un
  `.otf` à contours TrueType reste sélectionnable via « Tous les fichiers ».
- **Polices variables** : acceptées si elles ont un `glyf`, et rendues à leur
  instance par défaut. `fvar` n'est pas lu, donc aucun axe n'est réglable.
- **Fichier structurellement valide mais aux octets corrompus** : accepté. Le
  banc le vérifie explicitement (aucun plantage), mais un tel fichier peut
  produire des glyphes déformés. Détecter cela demanderait de vérifier les
  sommes `head.checkSumAdjustment`, ce qui n'est pas fait.
- **Aucun crénage, aucun hinting** — limite du rasterizer, pas de l'import.
- **Les polices importées sont listées, pas encore utilisables.** Le sélecteur
  de police appartient à la barre d'outils de l'éditeur, qui n'existe pas
  encore (voir « Dette technique »).

### Vérification : `gw_settings_tests`

```bash
./build/gw_settings_tests.exe assets/fonts/Inter-Regular.ttf /un/dossier
```

Couvre la persistance (aller-retour disque des trois réglages), les bornes du
corps de texte, l'import d'une police valide, le doublon suffixé, le retrait
(fichier supprimé, Inter non supprimable), et la douzaine de fichiers invalides
du tableau ci-dessus. **Ce banc écrit dans le vrai `settings.txt`** — son
emplacement vient de `SHGetKnownFolderPath` et n'est pas redirigeable par
variable d'environnement — et le restaure à la sortie.

---

## Moteur d'édition

C'était le manque structurant du projet : jusqu'ici l'application savait *lire*
un document, pas le modifier. Tout le reste de la feuille de route — barre de
formatage, mode Markdown, menu principal — en dépendait.

### La coupure : caractères d'un côté, lignes de l'autre

Le moteur est en deux moitiés, et la frontière entre elles est la décision
principale de cette passe.

| Fichier | Raisonne en | Dépend de |
|---|---|---|
| `doc/text_edit.cpp` | **caractères** : curseur, sélection, insertion, effacement, annulation | rien — module pur |
| `app/editor_view.cpp` | **lignes affichées** : retour à la ligne, défilement, clic, dessin | police, largeur de colonne |

La ligne de partage est simple à énoncer : *une ligne affichée n'existe pas
sans une largeur de colonne et des métriques de police.* Donc « aller à la
ligne du dessus », « début de ligne » et « où ai-je cliqué » sont des questions
de vue, pas de texte. La vue les résout en une `doc::Position` qu'elle pose
ensuite dans `TextEdit`.

Le bénéfice est direct : **toute la logique d'édition est vérifiable sans
fenêtre.** `gw_edit_tests` couvre 69 assertions — frontières UTF-8, sélections
à cheval sur plusieurs paragraphes, groupement des annulations — sans simuler
une seule frappe clavier ni charger une police.

### Positions en octets

Les offsets sont des index d'octets UTF-8, jamais des numéros de caractère.
Compter en points de code imposerait un parcours depuis le début du paragraphe
à chaque accès, pour un confort qui ne concerne que ce fichier. Les seules
fonctions qui franchissent une frontière de caractère sont
`previous_char_boundary` / `next_char_boundary`, et ce sont celles que les
tests attaquent en premier.

Conséquence vérifiée : sur `éàü€` — quatre caractères, neuf octets — un retour
arrière efface bien un caractère entier, pas un octet.

### Ce qui fonctionne

**Saisie.** `WM_CHAR` et `WM_KEYDOWN` sont deux chemins distincts, à dessein :
une touche est un *ordre*, un caractère saisi est une *donnée*. Windows a déjà
appliqué la disposition clavier, les touches mortes et AltGr quand `WM_CHAR`
arrive ; reconstruire cela depuis les codes virtuels serait refaire ce travail,
moins bien, et casserait toute disposition non française. Les paires de
substitution UTF-16 sont réassemblées avant de remonter, donc un caractère hors
du plan multilingue de base n'arrive jamais coupé en deux.

**Déplacements.** Flèches, mot à mot (`Ctrl`), début / fin de ligne *affichée*
(`Origine` / `Fin`), début / fin de document (`Ctrl`), page précédente /
suivante. Les déplacements verticaux mémorisent l'abscisse visée : descendre à
travers une ligne courte ne fait pas « fondre » le curseur vers la gauche.

**Sélection.** `Maj` + n'importe quel déplacement, glisser à la souris (avec
capture, donc sortir de la fenêtre ne casse rien), double-clic sur un mot,
`Ctrl+A`. Une flèche sans `Maj` replie la sélection sur son bord au lieu de
déplacer le curseur — c'est ce que font tous les éditeurs, et ne pas le faire
se remarque à la première utilisation.

**Presse-papiers.** Couper / copier / coller, en texte brut. Un collage
multiligne produit de vrais paragraphes : sans ce traitement, les `0x0A`
seraient insérés au milieu d'un paragraphe, invisibles à l'écran mais bien
présents dans le fichier. Les entrées du menu contextuel appellent exactement
les mêmes fonctions que les raccourcis — elles ne portent aucune logique
propre, c'est la condition pour que les deux chemins ne divergent jamais.

**Annulation.** `Ctrl+Z` / `Ctrl+Y` / `Ctrl+Maj+Z`, avec regroupement : une
phrase tapée d'un trait s'annule d'un seul coup. Un déplacement, un retour à la
ligne ou un collage ferment le groupe en cours.

**Enregistrement.** `Ctrl+S`, `Ctrl+Maj+S`, et une question à trois réponses
avant d'abandonner un document modifié. Un astérisque dans le titre signale le
document modifié — donc aussi dans la barre des tâches et Alt+Tab.

### Choix : instantané complet plutôt que journal d'opérations

L'annulation copie tout le document avant chaque mutation, au lieu de tenir un
journal d'opérations inversibles.

Le journal serait plus économique en mémoire, mais chaque nouvelle opération
devrait fournir son inverse exact — c'est la source classique des bugs
d'annulation, ceux où l'on retrouve un document *presque* identique après un
aller-retour. Ici l'aller-retour est exact par construction. Le coût réel est
une copie de quelques dizaines de kilo-octets, uniquement au moment d'une
mutation, et la pile est bornée à 256 entrées.

### Retour à la ligne

Coupure gloutonne, aux espaces et aux tirets. Un mot plus large que la colonne
est coupé au caractère plutôt que de déborder — sans quoi la boucle de mise en
page n'avancerait pas.

Volontairement pauvre : pas de dictionnaire de césure, pas de règles
typographiques françaises. Le découpage est **recalculé intégralement** à
chaque frappe, sans cache par paragraphe. Sur le plus gros document à portée de
main (33 Kio, 206 paragraphes), un passage complet reste sous la milliseconde,
et une frappe entraîne de toute façon une remesure pour le dessin. Un cache
incrémental est la première optimisation à faire si un document dix fois plus
gros devient courant — pas avant, parce qu'il ajoute une invalidation à tenir
juste.

### Vérification

`gw_edit_tests` — 69 assertions, **0 échec** — couvre le cœur pur :

| Domaine | Ce qui est vérifié |
|---|---|
| UTF-8 | avance / recul par caractère, position tombée au milieu d'une séquence, accents |
| Déplacements | franchissement de paragraphe, mot à mot avec espaces multiples, bornes du document |
| Sélection | repli sur un bord, texte extrait à cheval sur trois paragraphes, double-clic |
| Mutations | coupure et refusion de paragraphe, remplacement de sélection, collage multiligne |
| Annulation | regroupement de la frappe, coupure au déplacement, abandon de la branche de rétablissement |
| Bornage | position hors document, offset au-delà du paragraphe |

Le reste — retour à la ligne, sélection multi-lignes, curseur, mode sombre —
se vérifie en pixels : `gw_screen_render` écrit maintenant `editeur_clair.bmp`
et `editeur_sombre.bmp` en plus des quatre images existantes, à partir d'un
document de démonstration qui contient exactement les cas limites (paragraphe
replié plusieurs fois, paragraphe vide sélectionné, mot de 156 caractères sans
espace, sélection à cheval sur deux paragraphes).

### Limites assumées

- **Pas d'IME.** `WM_IME_*` n'est pas traité : la saisie en chinois, japonais
  ou coréen passe par une fenêtre de composition qui n'existe pas ici. Sur une
  disposition latine, y compris avec touches mortes et AltGr, tout passe par
  `WM_CHAR` et fonctionne.
- **Tabulation = quatre espaces.** Le modèle de document n'a pas de taquets, et
  une tabulation réelle s'afficherait comme un glyphe absent de largeur nulle.
- **Coupure de mot au-delà de l'ASCII** : tout point de code non-ASCII est
  compté comme une lettre. Exact pour les alphabets accentués, faux pour la
  ponctuation typographique (guillemets français, tirets cadratins), qui se
  retrouve collée au mot voisin lors d'un `Ctrl+Flèche`.
- **Rail de défilement indicateur, pas contrôle** : il ne se saisit pas à la
  souris. Il répond à la seule question qu'on se pose sans lui — « où suis-je
  dans le document » — et la molette couvre déjà le geste.
- **Pas de recherche, pas de remplacement, pas de numéros de ligne.**

### Vérification par une frappe réelle

Le chemin Win32 lui-même — `WM_CHAR`, le minuteur de clignotement, le
presse-papiers système — n'est couvert par aucun test automatisé : les harnais
console s'arrêtent au cœur pur (`gw_edit_tests`) et au rendu
(`gw_screen_render`). Il a été exercé **manuellement** : saisie, caractères
accentués, sélection à la souris et au clavier, copier-coller, annulation,
gras / italique / souligné depuis la barre. Tout répond.

C'est le seul morceau du programme dont la preuve soit un essai humain plutôt
qu'un banc — et c'est structurel : simuler `WM_CHAR` reviendrait à tester notre
propre simulation.

---

## Mise en forme

### Ce que le modèle porte

| Granularité | Attributs | Où |
|---|---|---|
| Paragraphe | niveau de titre (aucun / H1 / H2 / H3), liste (aucune / puce / numérotée), alignement (gauche / centre / droite) | trois champs de `doc::Paragraph` |
| Caractère | gras, italique, souligné, police | **plages** de `doc::Paragraph::spans` |

Les trois attributs de caractère se combinent librement — « gras italique
souligné » est un état légitime, donc trois booléens indépendants et non un
enum de variantes.

### Le codage des plages : longueurs, pas bornes

Un mot en gras au milieu d'une phrase normale doit être représentable. La mise
en forme de caractère est donc portée par une suite de segments consécutifs,
chacun décrit par **une longueur en octets et un style** :

```
texte  : "un mot gras ici"
spans  : [ {3, {}}, {8, {bold}}, {4, {}} ]
           └ "un "  └ "mot gras"  └ " ici"
```

**Pourquoi des longueurs et non des bornes `[début, fin)`.** Toute mutation de
texte déplace tout ce qui suit. Avec des bornes absolues, insérer un caractère
au début du paragraphe obligerait à décaler *chaque* borne de *chaque* plage —
un parcours complet, et surtout une occasion d'en oublier une. Avec des
longueurs, seule la plage qui contient le point d'insertion change, et les
suivantes restent correctes sans qu'on y touche. C'est la même raison qui fait
qu'un éditeur de texte stocke des « runs ».

**Invariants**, rétablis par `style_normalize()` après chaque opération :

- somme des longueurs == taille du texte du paragraphe ;
- aucune plage de longueur nulle ;
- jamais deux plages consécutives de style identique ;
- **un paragraphe entièrement au style par défaut a une liste vide.**

Ce dernier point est la forme canonique, et il porte deux propriétés : un
document sans mise en forme est exactement aussi léger qu'avant l'ajout de ce
fichier, et le test « ce paragraphe a-t-il un style ? » est trivial — ce qui
est précisément ce dont l'écriture `.odt` a besoin pour produire les anciens
fichiers à l'octet près.

Une liste incohérente (héritée d'un fichier qui ment) est **réparée** — tronquée
ou complétée au style par défaut — jamais rejetée. Un modèle incohérent doit se
réparer, pas se propager.

### Comportement face aux mutations de texte

C'est le point qui décide si la mise en forme est utilisable ou pénible. Les
quatre seules fonctions autorisées à muter un paragraphe (`doc/document.h`)
tiennent texte et plages à jour ensemble ; toucher `text` directement est
interdit, parce que la désynchronisation ne se voit qu'au rendu, plusieurs
frappes plus tard.

| Opération | Ce qui arrive au style |
|---|---|
| Insertion au milieu d'une plage | le texte inséré **hérite** de cette plage |
| Insertion en fin de plage grasse | hérite du caractère qui **précède**, donc reste gras — c'est ce qui permet de continuer à écrire en gras |
| Insertion en début de plage grasse | hérite du caractère qui précède, donc reste maigre |
| Insertion à l'offset 0 | hérite du caractère qui **suit**, faute d'un précédent |
| Insertion après une bascule sans sélection | style **imposé** par la bascule, pas hérité |
| Suppression traversant une frontière | les deux côtés se recollent, chacun gardant le sien |
| Suppression de toute une plage | les voisines fusionnent, retour à la forme canonique |
| Coupure de paragraphe | les deux moitiés gardent leur part du style **et** les attributs de paragraphe : couper une puce donne deux puces, couper un titre donne deux titres |
| Fusion de deux paragraphes | le survivant est celui du **haut** : ses attributs de paragraphe sont conservés, ceux du bas perdus |

La règle « la ligne qui suit un titre repasse en corps de texte » est un confort
de saisie, pas une propriété du modèle : elle appartient au chantier
clavier-first.

### Le style « en attente »

Cliquer sur **Gras** sans sélection ne modifie rien : cela **arme** le style de
la frappe suivante (`pending_style_`). Conséquences voulues :

- rien n'est empilé dans l'historique et le document n'est pas sali — il n'y a
  rien à annuler ;
- un déplacement de curseur **désarme** le style, sinon une bascule suivie d'un
  clic ailleurs écrirait en gras à l'autre bout du document ;
- la frappe consomme le style armé, puis la suite continue en gras par simple
  héritage — pas besoin de réarmer ;
- le style armé fait partie de l'instantané d'annulation : annuler une frappe
  grasse rend le curseur à l'état « gras armé ».

### Pas de taille de police par plage

`CharStyle` ne porte pas de taille, volontairement. Elle reste globale au
document — le corps par défaut des réglages, majoré par le niveau de titre du
paragraphe. Une taille par plage demanderait de porter une unité (points ?
pixels ? relatif ?) et de la faire survivre à un changement du corps par défaut :
trois décisions de format pour une granularité dont ce chantier n'a pas l'usage.
Le jour où elle arrive, elle s'ajoute comme un champ de plus, et le codage par
longueurs n'a pas à bouger.

### La typographie est dans la vue, pas dans le format

Le modèle dit « ce paragraphe est un H2 ». C'est `app/editor_view` qui décide
qu'un H2 se dessine à 1,55 fois le corps en semi-gras. Le rapport entre un
niveau de titre et une taille en pixels est une décision typographique, donc une
décision de vue : la changer ne doit toucher ni au format de fichier ni au
moteur d'édition.

| Niveau | Facteur |
|---|---|
| H1 | 1,95 |
| H2 | 1,55 |
| H3 | 1,25 |

Un titre est semi-gras **par construction**, que ses plages portent le gras ou
non ; retirer le niveau de titre rend le paragraphe à sa graisse d'origine.

Conséquence structurelle : deux lignes voisines n'ont plus la même hauteur.
Chaque `VisualLine` porte donc la sienne, et l'ordonnée d'une ligne se calcule
en cumulant les hauteurs depuis la première ligne affichée, au lieu d'une
multiplication. Le défilement reste compté **en lignes** et non en pixels :
c'est ce qui garde la molette prévisible quand on traverse un titre.

### Gras et italique : ce qui est réel, ce qui est synthétisé

Nous n'avons que deux fichiers Inter, Regular et SemiBold. Il n'y a pas
d'italique, et une police importée n'arrive qu'en un seul fichier.

| Demande | Réponse |
|---|---|
| gras sur Inter | **Inter SemiBold**, un vrai dessin |
| gras ailleurs | synthèse : la couverture est dilatée d'un pixel |
| italique partout | synthèse : le contour est **cisaillé avant rasterization** (0,21, soit ~12°) |

La synthèse est faite sur les **contours** ou sur la couverture, jamais sur
l'image finale : elle passe donc par le même rasterizer que tout le reste, et un
glyphe synthétisé se pose exactement comme un glyphe réel. C'est la différence
entre un faux gras propre et un texte redessiné deux fois avec un décalage, qui
bave et double les jointures. Le cisaillement part de la ligne de base, donc le
bas des lettres reste en place — cisailler autour du centre ferait dériver le
mot vers la gauche.

C'est une dégradation assumée et visible : un vrai italique dessiné n'a pas la
même forme qu'un romain penché (le « a » italique change de squelette). Le jour
où des fichiers Inter Italic seront ajoutés, il suffira de les charger dans
`app/fonts.cpp` et la synthèse s'arrêtera d'elle-même pour Inter.

**Une police nommée mais introuvable retombe sur Inter au rendu, sans que le
document perde le nom.** Le fichier peut revenir.

### La barre d'outils

`app/format_bar` est de la même nature que `app/chrome` : ce n'est pas une
fenêtre, ni une barre d'outils système. C'est une bande dessinée dans le même
framebuffer que le reste et une poignée de rectangles pour le hit-test. Aucun
HWND enfant, donc aucun contrôle natif à thémer, et elle se présente par le même
backend que le contenu.

```
B  I  U │ H1 H2 H3 │ •  1. │ ⯇ ≡ ⯈ │ [ Inter ⌄ ]
```

**Elle ne porte aucune logique d'édition.** Un clic rend une `FormatAction` ;
c'est `App` qui la traduit en appel à `doc::TextEdit`, exactement comme il
traduit une touche du clavier ou une entrée du menu contextuel. C'est la
condition pour que les trois chemins ne divergent jamais — et c'est aussi ce qui
fait que **la mise en forme entre dans l'historique d'annulation sans mécanisme
dédié** : elle passe par les mêmes fonctions, donc par le même `push_undo`.

**État affiché, pas état détenu.** La barre ne se souvient de rien : elle reçoit
à chaque dessin l'état réel du curseur et s'y conforme. Sans cela il faudrait la
resynchroniser après chaque frappe, chaque clic, chaque annulation — et
l'oublier une seule fois donnerait un bouton qui ment.

Détails de comportement :

- **Titres et listes sont des bascules** : recliquer sur le niveau déjà actif
  remet le paragraphe en corps de texte. Sans cela il n'y aurait aucun moyen de
  défaire un titre, et il faudrait un bouton « corps de texte » qui ne servirait
  qu'à ça.
- **L'alignement est exclusif** : trois états, pas trois bascules.
- Une bascule de caractère sur une sélection **à moitié** grasse la met
  **entièrement** en gras ; elle ne l'éteint que si toute la sélection la porte
  déjà.
- Les libellés sont dessinés dans la variante qu'ils appliquent — le B est
  semi-gras, le I est penché, le U est souligné. Un pictogramme aurait dit la
  même chose en moins bien. Seul l'alignement est une icône : c'est le seul cas
  sans lettre qui le représente.
- Le sélecteur de police réutilise le **menu contextuel** existant : c'est déjà
  un menu déroulant complet (clavier, survol, fermeture au clic extérieur,
  recadrage aux bords), et en écrire un second n'apporterait qu'une seconde
  série de bugs.
- Raccourcis clavier `Ctrl+B` / `Ctrl+I` / `Ctrl+U`, qui appellent la **même**
  fonction que les boutons.

### Vérification

| Cible | Ce qu'elle prouve | Résultat |
|---|---|---|
| `gw_style_tests` | modèle enrichi seul, sans interface : application sur une plage, chevauchement de deux styles, héritage à l'insertion (milieu, deux frontières, offset 0, style imposé), suppression traversant une frontière, coupure/recollage, bascules, style en attente, attributs de paragraphe sur sélection multiple, annulation, fusion de paragraphes stylisés, police par plage, réparation de plages incohérentes | **40 vérifications, 0 échec** |
| `gw_edit_tests` | moteur d'édition inchangé — **aucune régression** | **69 vérifications, 0 échec** |
| `gw_doc_roundtrip` | round-trip du formatage, compatibilité ascendante et descendante | **25 vérifications, 0 échec** |
| `gw_screen_render` | rendu réel : `mise_en_forme_clair.bmp` / `_sombre.bmp` | 8 images |

La dernière ligne est celle qui compte pour un chantier d'apparence : un test
unitaire peut prouver que le modèle porte « ce mot est en gras », **pas que le
mot sort effectivement plus gras à l'écran**. L'image produite montre les trois
niveaux de titre, les deux types de liste (dont un point replié sur deux lignes,
retrait tenu sur la continuation), les trois alignements, les trois attributs de
caractère isolés et combinés, et la barre d'outils avec Gras allumé parce que le
curseur est posé dans une plage grasse.

---

## Mode clavier-first

### Ce que c'est, et ce que ce n'est pas

Un réglage — *Paramètres → Mode clavier (Markdown en direct)* — qui masque la
barre de mise en forme et active la reconnaissance d'une syntaxe Markdown
légère **au fil de la frappe**. Il ne s'agit pas d'un second format de
document : la conversion produit exactement les mêmes champs de
`doc::Paragraph` et les mêmes plages de `doc/style.h` que la barre d'outils.

C'est la propriété qui justifie tout le reste. Basculer d'un mode à l'autre
n'a **rien à convertir**, donc rien à perdre : un texte mis en forme au clavier
s'affiche correctement quand la barre revient, et réciproquement. Le test
`clavier-first et barre d'outils : modèle de document identique` de
`gw_style_tests` est là pour que cette égalité ne se perde pas en silence.

### Formes reconnues

| Syntaxe | Résultat | Déclencheur |
|---|---|---|
| `**texte**` | gras | le second `*` fermant |
| `*texte*`, `_texte_` | italique | le marqueur fermant |
| `# `, `## `, `### ` en tête de paragraphe | H1 / H2 / H3 | l'espace |
| `- ` en tête de paragraphe | puce | l'espace |
| `1. ` (n'importe quel nombre) en tête | liste numérotée | l'espace |

Et rien d'autre. Pas de liens, pas de citations, pas de code inline : chacun
demanderait soit un champ de modèle qui n'existe pas (URL, niveau de citation),
soit une police à chasse fixe que l'application ne fournit pas.

### Les marqueurs disparaissent

Décision retenue : **live preview**, pas source visible. Une fois la syntaxe
reconnue, les `**` ou le `# ` sont retirés du texte et seul le formatage
subsiste.

Conséquences assumées :

- le fichier enregistré ne contient **aucun marqueur** — c'est du vrai style
  `.odt`, pas de la syntaxe ;
- la conversion n'est défaisable que par `Ctrl+Z`. L'instantané d'annulation
  est pris **avant** le retrait des marqueurs, donc annuler rend le texte tel
  qu'il a été tapé, marqueurs compris. C'est le seul moyen de revoir la syntaxe
  une fois qu'elle a disparu, et c'est ce qu'attend quelqu'un qui vient de
  taper `**` sans le vouloir.

### Où vit le code, et pourquoi là

`doc/markdown_live.{h,cpp}` est un **module pur**, au même titre que
`doc/text_edit.cpp` et `doc/style.cpp` : ni Win32, ni pixels, ni police. Il ne
connaît même pas le curseur du document — juste un paragraphe et un offset.
C'est ce qui permet de le vérifier entièrement dans `gw_style_tests`, sans
simuler une seule frappe réelle.

Ce n'est **pas un parseur Markdown**. Il ne lit jamais un document entier, ne
produit jamais d'arbre, et répond à une seule question, posée après chaque
caractère inséré : *le caractère qui vient d'être tapé ferme-t-il une syntaxe ?*

La conversion elle-même passe par `TextEdit::apply_markdown_live()`, pour la
même raison que les bascules de la barre d'outils y passent : une conversion
est une mutation du document, elle doit entrer dans l'historique. Sinon un
`Ctrl+Z` juste après une conversion sauterait par-dessus et remonterait avant
la frappe du mot. Et la mutation du texte passe par `paragraph_erase()` et
`style_apply()` — les mêmes fonctions que partout ailleurs, ce qui fait qu'un
contenu déjà souligné ou dans une autre police garde ses autres attributs.

### Faux positifs : la règle est de ne rien faire

Le coût d'un faux négatif est que l'utilisateur tape le marqueur une fois de
plus. Le coût d'un faux positif est du texte qui disparaît sous ses doigts. La
détection rend donc « aucune conversion » dans tous les cas douteux :

| Cas | Comportement | Raison |
|---|---|---|
| `*mot` sans fermeture | rien | pas de paire |
| `****` | rien | contenu vide |
| `3 * 4 * 5` | rien | le contenu est bordé d'espaces (règle CommonMark) |
| `nom_de_variable` | rien | le `_` ouvrant est interne à un mot |
| `mot**gras**` | converti | l'astérisque n'est **pas** soumis à la règle du mot : seul le `_` apparaît couramment à l'intérieur d'un identifiant |

### Limite connue : l'héritage des attributs de paragraphe

Un paragraphe créé par `Entrée` hérite des attributs de celui qu'on vient de
couper (`paragraph_split`) — c'est voulu pour les listes, « couper une puce en
deux donne deux puces », et c'est testé comme tel. Mais **cela vaut aussi pour
le niveau de titre** : passer à la ligne après un H1 donne un second H1, et il
n'existe aucun moyen au clavier de revenir au corps de texte.

Ce comportement est antérieur à ce chantier et partagé avec la barre d'outils ;
il n'a donc pas été modifié ici. Le mode clavier le contourne à moitié : taper
`## ` dans un paragraphe déjà titré **remplace** le niveau au lieu d'être
refusé, et `1. ` dans une puce bascule en liste numérotée. Sans cette
souplesse, un titre hérité rendrait toute la syntaxe de paragraphe inopérante
pour le reste du document.

L'image `clavier_first_*.bmp` produite par `gw_screen_render` montre le cas :
la dernière ligne reste numérotée par héritage.

---

## Menu principal

### Une bande de titres, pas un menu

`app/menu_bar.{h,cpp}`, même nature que `TitleBar` et `FormatBar` : ce n'est pas
une fenêtre, ni un menu système. C'est une bande dessinée dans le même
framebuffer que le reste, et trois rectangles qui servent au hit-test.

La différence avec une barre de menu classique est que **cette classe ne
contient aucune entrée**. Elle ne connaît que les titres. Le contenu déroulant
est un `ContextMenu` — exactement celui du clic droit et du sélecteur de police.
Un menu déroulant complet (clavier, survol, fermeture au clic extérieur,
recadrage aux bords de la fenêtre) existait déjà dans le programme ; en écrire
un second n'aurait apporté qu'une seconde série de bugs. `MenuBar` ne fait donc
que dire **quel** menu a été cliqué et **où** l'ancrer.

### Contenu

| Menu | Entrées |
|---|---|
| Fichier | Nouveau, Ouvrir…, Enregistrer, Enregistrer sous…, Retour à l'accueil |
| Édition | Annuler, Rétablir, Couper, Copier, Coller, Sélectionner tout |
| Affichage | Mode sombre, Mode clavier (Markdown) — cochés selon l'état |

Aucune entrée ne porte de logique propre. Toutes passent par
`App::run_menu_action()`, qui est **le même** gestionnaire que celui du menu
contextuel, et qui appelle la fonction qu'appellerait le raccourci clavier
équivalent. Trois gestes partagés par au moins deux chemins d'entrée —
`undo_edit()`, `redo_edit()`, `select_all_text()` — ont été extraits pour cette
raison : ce n'est pas un raffinement, c'est ce qui permet à une entrée de menu
de n'avoir rien à elle.

Les deux bascules d'affichage passent par `run_settings_action()` et non par
`Settings` directement : c'est ce chemin qui persiste le réglage, refait la mise
en page et redessine. Le menu n'a pas à connaître ces trois étapes.

Les entrées sont grisées selon les mêmes règles que le menu contextuel :
Annuler / Rétablir suivent `can_undo()` / `can_redo()`, Couper / Copier suivent
la présence d'une sélection. Une entrée active qui ne ferait rien laisserait
croire à une panne.

`Nouveau` et `Ouvrir…` n'affichent aucun raccourci : l'application n'en a pas
pour ces deux actions, et annoncer un `Ctrl+N` qui ne ferait rien serait pire
que la colonne vide.

### Deux détails de comportement

**Elle agit à l'enfoncement**, contrairement à tous les autres boutons de
l'application. C'est la convention de toutes les barres de menu, et surtout
c'est ce qui permet d'ouvrir un menu, de glisser jusqu'à l'entrée voulue et de
relâcher dessus. Recliquer sur le titre déjà ouvert referme, sinon il n'y
aurait aucun moyen de fermer en visant là où on vient de cliquer.

**Elle est discrète au repos** : trois mots gris sur le fond de page, aucun
filet, aucune surface. La bande ne se matérialise que sous le curseur ou quand
un déroulant est ouvert. C'est ce qui la rend compatible avec le mode clavier,
où tout le reste de la barre d'outils a disparu — elle y est d'ailleurs le seul
élément de chrome conservé, et le seul accès à *Enregistrer sous* en dehors du
raccourci.

### Portée : l'éditeur seulement

Ni l'accueil ni les paramètres ne la portent. L'accueil a déjà ses propres
boutons pour tout ce que le menu Fichier propose, et le menu Édition n'aurait
rien à y commander : une barre de menu grisée aux trois quarts vaut moins que
pas de barre du tout.

### Ce qui n'est pas vérifié automatiquement

`gw_screen_render` produit `menu_ouvert_*.bmp` : la bande, un déroulant
déployé, son ancrage sous le titre, le titre en surface pleine et les entrées
grisées. Cela vérifie les **pixels**, pas les **actions** — comme pour le menu
contextuel avant lui, le déclenchement effectif de chaque entrée relève du test
manuel. La liste d'entrées utilisée par cette image est un échantillon de
rendu ; la liste qui fait foi est construite par `App::open_main_menu()` et
nulle part ailleurs.

---

## Rendu de texte

### Tables TTF lues

`head` (unitsPerEm, indexToLocFormat) · `maxp` (numGlyphs) · `cmap` (formats 4
et 12) · `loca` · `glyf` (contours simples **et composites**) · `hhea` + `hmtx`
(métriques horizontales et verticales globales).

Toutes les lectures sont bornées : un fichier tronqué produit un échec propre
avec un message, jamais une lecture hors buffer. Les polices **OpenType/CFF
(`OTTO`) sont refusées explicitement** — le parser ne lit que des contours
`glyf`.

### Règle de remplissage : nonzero winding

Retenue plutôt que even-odd, et ce n'est pas une préférence de style. Les
contours TrueType sont orientés (extérieur horaire, intérieur anti-horaire)
précisément pour que le nonzero creuse les contreformes. Avec even-odd, deux
contours extérieurs qui se chevauchent — fréquent après décomposition d'un
composite, par exemple un accent qui mord sur la lettre — laisseraient un trou
à l'intersection.

C'est aussi ce qui rend correcte la croix du bouton Fermer : ses deux barres
diagonales sont émises dans le même sens, donc leur intersection reste pleine.

Le balayage échantillonne au centre du pixel (`y + 0.5`), avec un intervalle
semi-ouvert `[y0, y1)` sur les arêtes pour qu'un sommet partagé ne compte pas
deux fois.

### Atlas

Packing par étagères (*shelf*), image 8 bits en RAM, uploadée en texture côté
OpenGL et lue directement côté GDI. Un atlas par couple (graisse, corps), créé
à la demande et jamais libéré : une dizaine de couples sont utilisés, et un
atlas de 512 px coûte 256 Kio.

---

## Décompression — inflate maison

Un seul décodeur DEFLATE sert deux besoins qui, jusqu'ici, avaient chacun leur
compromis : le PNG passait par GDI+ (codec système), et aucun `.odt` produit par
LibreOffice ou Word n'était lisible, faute de savoir décompresser.

`compress/inflate.cpp` — 431 lignes — implémente la RFC 1951 : les trois types
de blocs (`stored`, Huffman fixe, Huffman dynamique), plus l'enveloppe zlib de
la RFC 1950 avec vérification Adler-32. **Décompression seulement** : l'écriture
continue en `stored`, choix inchangé et documenté.

Le décodage Huffman se fait bit à bit, à la façon de `puff`, sans table de
correspondance. C'est plus lent qu'une table, mais les flux manipulés font
quelques dizaines de kilo-octets et cela supprime tout dimensionnement de table
à justifier.

### Preuves

**Les vecteurs de test ne sont pas produits par notre code.** Ils viennent du
module `zlib` de Python — une implémentation de référence indépendante. Un
décodeur validé contre ses propres sorties ne prouverait rien.

| Vecteur | Clair | Compressé | Premier bloc | Résultat |
|---|---|---|---|---|
| `stored` | 740 o | 745 o | stored | identique |
| `fixed` | 3 o | 5 o | Huffman fixe | identique |
| `dynamic` | 13 200 o | 102 o | Huffman dynamique | identique |
| `mixed` | 97 000 o | 70 212 o | stored, non final → blocs multiples | identique |

L'enveloppe zlib est vérifiée de même, l'Adler-32 calculé est comparé à celui de
Python, et un Adler-32 falsifié est **rejeté** au lieu d'être ignoré.

**PNG.** Le décodeur maison a d'abord été comparé à GDI+ sur les deux logos
réels : **256 000 et 227 529 pixels strictement identiques**, zéro différence.
Cette comparaison a ensuite été remplacée par une référence sur fichier,
produite par un décodeur PNG écrit en Python au-dessus de `zlib` — pour que le
test survive au retrait de GDI+, et reste valable là où GDI+ n'existe pas.

**ODT externe.** Une archive dont `content.xml` est compressé en méthode 8
(33 247 → 1 004 octets), produite par `zipfile` de Python, est lue correctement :
206 paragraphes, entités XML décodées. Elle s'ouvre aussi depuis l'interface.
Avant cette passe, elle était purement illisible.

Le vecteur n'est pas versionné — il est régénéré par
`tools/gen_external_odt.py <répertoire>`, à lancer avant `gw_doc_roundtrip` :
sans lui le test s'annonce `[SKIP]` et le DEFLATE n'est jamais exercé.

**Génération des vecteurs.** Aucun de ces fichiers n'est versionné — ils sont
tous régénérés par `tools/gen_compress_vectors.py <répertoire>`, à lancer avant
`gw_compress_tests` :

```bash
python tools/gen_compress_vectors.py build/vectors
./build/gw_compress_tests.exe build/vectors assets/branding
```

Le script produit les quatre vecteurs DEFLATE, l'enveloppe zlib avec son
Adler-32 en clair, et les deux références PNG — ces dernières via un décodeur
PNG écrit dans le script lui-même au-dessus de `zlib`, qui ne partage pas une
ligne avec `image/png.cpp`. Il **vérifie** au passage que chaque flux a bien le
type de premier bloc attendu et la taille documentée ci-dessus : le volume du
test de robustesse (102 troncatures, 306 mutations) dérive directement de la
taille de `dynamic.deflate`, donc une dérive de version de `zlib` fait échouer
le script bruyamment plutôt que de réduire le banc en silence.

**Robustesse.** Aucun plantage sur : les 102 troncatures possibles d'un flux
valide, 306 mutations d'un octet (trois masques à chaque position), et
2 000 flux aléatoires. Aucune troncature n'a été acceptée par erreur. Toutes les
sorties sont bornées à 64 Mio, et une distance de référence pointant avant le
début de la sortie est rejetée — c'est ce contrôle qui empêche une lecture hors
tampon sur un flux forgé.

### Statut de GDI+ : retiré

`platform/image.h` et `platform/image.cpp` sont supprimés, `gdiplus` n'apparaît
plus ni dans les sources ni dans les chaînes du binaire. Le décodage PNG est
désormais dans `image/`, module **pur** : il ne dépend d'aucune API système et
porte tel quel.

C'est aussi la réponse à une question ouverte de la fiche Linux — « lib système
équivalente à GDI+, ou décodeur maison ? ». Elle ne se pose plus.

### Limites assumées du décodeur PNG

Sous-ensemble volontairement réduit à ce qui sert : profondeur **8 bits** par
composante, types de couleur 0, 2, 3, 4 et 6, **pas d'entrelacement Adam7**.
Le 16 bits et l'Adam7 sont refusés avec un message explicite, jamais mal
interprétés. Les CRC de chunks sont vérifiés.

---

## Formats de fichier

### `.txt`

**Lecture — heuristique d'encodage**, dans l'ordre :

1. BOM UTF-8 → retiré, lu en UTF-8 ;
2. BOM UTF-16 LE ou BE → **refus explicite** avec un message ;
3. sans BOM, UTF-8 valide → lu en UTF-8 ;
4. sans BOM, UTF-8 invalide → supposé CP1252, transcodé en UTF-8.

Ce n'est pas une détection fiable et cela n'en a pas la prétention : aucune
heuristique ne lève l'ambiguïté dans le cas général. Elle est simplement
conservatrice — la validation UTF-8 est stricte (surencodages, demi-paires et
code points hors Unicode rejetés), donc du CP1252 réel y échoue presque
toujours, et un fichier UTF-8 valide n'est jamais mal interprété.

**Écriture : UTF-8 sans BOM.** Un BOM en UTF-8 est optionnel, non recommandé
par Unicode, et casse plus d'outils qu'il n'en aide.

**Fins de ligne** : CRLF, LF et CR isolé sont tous acceptés en lecture ;
l'écriture normalise en **CRLF**, convention Windows et attente du Bloc-notes
historique. La cible étant Windows, écrire du LF nuirait sans rien apporter.

### `.odt` — sous-ensemble maison

**Ce n'est pas de l'OpenDocument conforme.** L'archive contient :

| Entrée | Contenu |
|---|---|
| `mimetype` | `application/vnd.oasis.opendocument.text`, **premier et non compressé** |
| `META-INF/manifest.xml` | les trois entrées, rien de plus |
| `styles.xml` | racine vide : aucun style **nommé** n'est modélisé |
| `content.xml` | un `<text:p>` par paragraphe, texte échappé, plus les attributs de mise en forme ci-dessous |

#### Schéma de mise en forme

Toute la mise en forme vit dans un espace de noms maison, déclaré sur la
racine de `content.xml` :

```xml
xmlns:gw="urn:gastinel:write:format:1.0"
```

**Pourquoi un espace de noms à nous plutôt que les attributs ODF.** L'ODF
exprime la mise en forme par des styles *nommés*, définis à part dans
`<office:automatic-styles>`, avec héritage et familles. Reproduire ce mécanisme
pour porter trois booléens serait écrire un tiers d'ODF sans pouvoir tenir les
deux autres tiers — donc produire un fichier qui *a l'air* conforme et ne l'est
pas. Sous `gw:`, le contrat est explicite : la spécification XML impose à un
lecteur d'ignorer un espace de noms qu'il ne connaît pas, donc LibreOffice
ouvrira le texte sans la mise en forme, ce qui est exactement la dégradation
annoncée.

Mise en forme de **paragraphe**, en attributs de `<text:p>` :

| Attribut | Valeurs | Absent |
|---|---|---|
| `gw:heading` | `1` `2` `3` | corps de texte |
| `gw:list` | `bullet` `number` | pas de liste |
| `gw:align` | `center` `right` | aligné à gauche |

Mise en forme de **caractère**, en attributs de `<text:span>` :

| Attribut | Valeurs | Absent |
|---|---|---|
| `gw:b` | `1` | pas de gras |
| `gw:i` | `1` | pas d'italique |
| `gw:u` | `1` | pas de souligné |
| `gw:font` | nom d'affichage | police par défaut |

Exemple complet :

```xml
<text:p gw:heading="2" gw:align="center">Un <text:span gw:b="1" gw:u="1">titre</text:span> centré</text:p>
```

**Les valeurs par défaut ne sont jamais écrites**, et une plage au style par
défaut sort en texte nu, sans `<text:span>`. Conséquence directe et voulue : un
document sans aucune mise en forme produit **exactement le même `content.xml`
qu'avant ce chantier**. Un `.odt` écrit par une version précédente se relit donc
sans style, et un `.odt` écrit aujourd'hui sans style est indistinguable d'un
ancien. Les deux sens sont vérifiés par `gw_doc_roundtrip`.

**Pas de taille de police par plage** : le corps reste global au document (le
réglage), majoré par le niveau de titre du paragraphe. Voir la section
« Mise en forme » pour la justification.

**Lecture : `stored` et `deflate`.** Les entrées compressées passent par
l'inflate maison, donc un `.odt` de LibreOffice ou de Word se lit désormais.
Le CRC-32 de l'entrée est vérifié après décompression, quelle que soit la
méthode.

**Écriture : `stored` uniquement**, choix inchangé. Écrire du deflate demanderait un compresseur
maison complet (Huffman + LZ77) pour gagner quelques kilo-octets sur des
fichiers texte minuscules. « Stored » est une méthode parfaitement légale et la
contrainte OpenDocument sur `mimetype` s'en trouve satisfaite par construction.
Le lecteur, lui, sait décompresser.

Autres limites : pas de zip64 (4 Gio, 65535 entrées), pas de `<text:s>` ni
`<text:tab>` — les espaces multiples et tabulations sont écrits tels quels, et
un lecteur ODF strict a le droit de les normaliser. L'aller-retour Gastinel
Write → Gastinel Write, lui, les conserve.

**Périmètre de lecture** : relire un fichier écrit par Gastinel Write, avec ou
sans mise en forme. Le parseur est un balayage textuel des `<text:p>`, pas un
parseur XML, et il **n'interprète aucun style ODF**. Relire un `.odt` de
LibreOffice ou Word est **hors périmètre** : en pratique un fichier simple
ressortira en texte brut sans mise en forme, ce n'est ni garanti ni testé.

### Preuve de round-trip

`gw_doc_roundtrip` écrit un document, le relit et compare paragraphe par
paragraphe — **texte, attributs de paragraphe et plages de style**. Se contenter
du texte ferait passer le round-trip du formatage pour réussi alors qu'il aurait
tout perdu ; la comparaison est donc complète.

**25 vérifications, toutes au vert** :

- round-trip TXT et ODT du texte brut (accents multi-octets, caractères à
  échapper en XML, paragraphe vide, espaces multiples, tabulation) ;
- round-trip ODT de **toutes les combinaisons de mise en forme** : les trois
  niveaux de titre, les deux types de liste, les trois alignements, les trois
  attributs de caractère isolés puis combinés, une police par plage, une plage
  collée au début et une collée à la fin, des caractères à échapper *à
  l'intérieur* d'un `<text:span>`, des accents à cheval sur une frontière de
  plage, et un paragraphe vide mais stylisé ;
- **compatibilité ascendante** : un `content.xml` de l'ancien format (aucun
  `gw:`, aucun `<text:span>`) s'ouvre sans erreur et sans le moindre style
  appliqué ;
- **corollaire inverse** : un document sans style produit un `content.xml` qui
  ne contient ni `gw:` ni `<text:span>` — les fichiers existants ne sont pas
  alourdis ;
- structure de l'archive, position et lisibilité en clair de `mimetype`, les
  quatre branches de détection d'encodage, la normalisation des fins de ligne.

Validation croisée supplémentaire, hors du projet : l'archive produite est lue
sans erreur par `zipfile` de Python (CRC vérifiés, quatre entrées en méthode 0),
et un `.odt` produit *par* Python est relu correctement par notre parseur.

---

## Profil mémoire

Mesuré au repos (accueil affiché, aucune note ouverte, fenêtre 1000×700), sur
Intel UHD 770 / Windows 11. Outil : `tools/memprofile.ps1`, qui parcourt
l'espace d'adressage avec `VirtualQueryEx` et classe les régions engagées par
type — l'équivalent de ce que fait VMMap, sans dépendance à installer.

```bash
powershell -File tools/memprofile.ps1 -ProcessId <pid> -Label "essai"
```

### Répartition

| Poste | Working set | Optimisable ? |
|---|---|---|
| Pilote OpenGL Intel | **~66 Mo** | Non — hors de notre code |
| DLL système + runtime C++ (39 modules) | ~13 Mo | Non — incompressible |
| Tas C++ applicatif | 5,2 Mo → **4,2 Mo** | Oui, corrigé |
| Texture OpenGL (dans la zone GPU du pilote) | 4,0 Mo → **2,7 Mo** | Oui, corrigé |
| **Total** | **88,4 Mo → 86,0 Mo** | |

Le poste dominant a été isolé en mesurant le même état sur les deux backends :
**88,4 Mo en OpenGL contre 22,7 Mo en GDI**. Les 66 Mo d'écart sont
`igc64.dll` (82,7 Mo d'image), `igxelpicd64.dll` (20,4 Mo), `igdgmm64.dll` et
`igdml64.dll`, plus une région de 9,94 Mo en `WRITECOMBINE` — la mémoire GPU
mappée par le pilote. Rien de tout cela ne nous appartient.

Détail du tas applicatif (compté en remplaçant temporairement `operator new`) :
framebuffer 2,67 Mo · atlas 1,25 Mo · polices (2 TTF + cmap + loca) 0,87 Mo ·
divers 0,45 Mo.

### Corrections appliquées

**1. Atlas de glyphes alloués à la demande.** Ils étaient pré-alloués en
512×512 (256 Kio chacun) alors que la mesure montrait **12 à 23 lignes
réellement occupées, soit 96,6 % de gâchis** — et ce gâchis était payé une fois
par couple (graisse, corps). L'atlas démarre maintenant à 512×64 et double sa
hauteur quand une étagère déborde, jusqu'à 4096 lignes. La croissance ne recopie
rien : l'image est stockée ligne par ligne à largeur fixe, donc les pixels déjà
écrits occupent déjà le préfixe exact du tampon.

*Gain : 1,25 Mo → 0,156 Mo.* Vérifié sans corruption sur 690 glyphes insérés
dans un atlas passé de 64 à 1024 lignes. La borne haute a été **augmentée**
(512 → 4096 lignes), donc la capacité pour les polices importées est plus
grande qu'avant, pas moindre.

**2. Textures non puissance de deux quand le pilote les accepte.** Le backend
arrondissait à la puissance de deux englobante, contrainte d'OpenGL 1.1 : une
fenêtre maximisée de 1920×1028 demandait une texture 2048×2048, soit **16 Mo
pour 7,5 Mo utiles**. Le support NPOT est désormais détecté au runtime
(cœur d'OpenGL depuis 2.0, sinon extension), avec repli sur la puissance de deux
inchangé. La texture ne fait que grandir, jamais rétrécir : coller au pixel près
provoquerait sinon une réallocation à chaque pixel d'un glisser de
redimensionnement.

*Gain : 4,0 → 2,7 Mo en fenêtré, 16,0 → 7,5 Mo maximisé.*

### Résultat

| État | Avant | Après | Gain |
|---|---|---|---|
| OpenGL, 1000×700 | 88,4 Mo | **86,0 Mo** | −2,5 Mo |
| OpenGL, maximisée | 146,1 Mo | **133 Mo** | −13 Mo |
| GDI, 1000×700 | 22,7 Mo | **21,6 Mo** | −1,1 Mo |
| Tas C++ seul | 5,24 Mo | **4,15 Mo** | −21 % |

### Pistes écartées, mesures à l'appui

- **Fuite d'objets GDI : aucune.** 13 objets GDI et 11 USER, identiques avant et
  après 180 redimensionnements. La mémoire privée se stabilise à 11,33 Mo et n'a
  plus bougé sur trois cycles successifs — c'est de la rétention d'allocateur
  ponctuelle, pas une fuite.
- **Double allocation entre backends : inexistante.** Le backend GDI n'est pas
  créé quand OpenGL réussit ; seul l'écran de chargement en crée un, libéré à sa
  fermeture.
- **GDI+ : le sujet a disparu avec le codec.** À l'époque de la mesure, GDI+
  était bien déchargé après le décodage mais laissait `WindowsCodecs.dll`
  (2,18 Mo) chargé, pour ~0,8 Mo de working set résiduel. GDI+ ayant depuis été
  retiré du projet, ni l'un ni l'autre n'apparaît plus dans la liste des modules
  — vérifié au repos. Le gain attendu ne se lit toutefois pas dans le total : la
  re-mesure au repos donne **86,1 Mo**, soit la valeur du tableau ci-dessus.

---

## Dette technique — assumée, à traiter plus tard

1. **Aucun crénage.** Les tables `kern` et `GPOS` ne sont pas lues. L'avance
   vient de `hmtx` seule, sans ajustement par paire : `AV`, `To`, `Wa` sont
   visiblement trop espacés. C'est le défaut le plus voyant.
2. **Aucun antialiasing.** Couverture binaire (0 ou 255), d'où des escaliers
   sur les diagonales. Le chemin de mélange *source-over* est déjà écrit dans
   `blit_glyph` et `blit_coverage` : passer à une couverture analytique ne
   demandera pas de changer le compositeur. C'est aussi ce qui interdit le
   texte sous ~11 px, et donc ce qui a déplacé l'extension hors de l'icône de
   fichier.
3. **Aucun hinting.** Les instructions `fpgm` / `prep` / `cvt` sont ignorées.

Autres limites connues :

- glyphes composites : seul `ARGS_ARE_XY_VALUES` est géré ;
- `cmap` : seuls les formats 4 et 12 ;
- pas de shaping (ni ligatures, ni marques, ni RTL), pas d'écriture verticale ;
- pas de césure ni de règles typographiques françaises dans le retour à la ligne ;
- pas de saisie IME (chinois, japonais, coréen).

**Le manque structurant est maintenant le formatage, plus l'édition.** Le moteur
d'édition existe (voir *Moteur d'édition*) : saisie, curseur, sélection, retour
à la ligne, défilement, annulation, enregistrement. Ce qui manque désormais est
le modèle : `doc::Paragraph` ne porte que du texte brut, aucun champ de style.

Conséquence directe : une barre d'outils de formatage (gras, titres, listes,
alignement) a maintenant une sélection et une frappe sur quoi agir, mais rien
où écrire son résultat. Le prochain prérequis est l'enrichissement du modèle de
document et l'extension du sous-ensemble `content.xml` qui va avec.

---

## Police

`assets/fonts/Inter-Regular.ttf` et `Inter-SemiBold.ttf`, extraits de la
release officielle [Inter v4.1](https://github.com/rsms/inter).

Le reste de l'écosystème n'embarque Inter qu'en **woff2**
(`_Site/assets/fonts/`, `GastinelWeatherStudio/src/fonts/`), inexploitable ici :
woff2 est compressé en Brotli, ce que le parser maison ne sait pas — et ne doit
pas — décompresser.

**Licence : SIL Open Font License 1.1**, texte complet dans
`assets/fonts/LICENSE.txt`. La redistribution avec le binaire est autorisée à
condition de conserver ce fichier dans le paquet livré. **À vérifier avant
toute distribution publique** : la OFL interdit de vendre la police seule et
impose qu'un dérivé renommé ne réutilise pas le nom réservé.

---

## Préparation portage — audit de portabilité

**Ce n'est pas un portage.** Les fiches Linux, Windows 7/8 et Vista/XP sont
explicitement marquées « à garder de côté » tant que le socle Windows 10/11
bouge. Cette section ne fait que produire le matériau d'audit que la fiche
Vista/XP réclame comme préalable à l'écriture d'un vrai prompt.

Méthode : lecture de la table d'imports du binaire réellement produit
(`objdump -p build/gastinel_write.exe`), pas une relecture du code — ce qui
compte est ce qui est lié, pas ce qu'on croit avoir appelé.

### Obstacle n°1, avant toute question d'API : le runtime C

Le binaire importe `api-ms-win-crt-*.dll` : la chaîne utilisée est
**MinGW-w64 UCRT**. L'Universal CRT est livré avec Windows 10 et au-delà ; sur
Windows 7 / 8 / 8.1 il faut le redistribuable (KB2999226), et il n'existe pas
pour XP. Aucune modification du code ne contourne cela : c'est un choix de
chaîne de compilation. La variante MinGW-w64 **MSVCRT** vise les Windows plus
anciens. À trancher avant tout le reste — c'est plus structurant que n'importe
quelle API listée ci-dessous.

### Obstacle n°2 : deux symboles Vista+, trouvés en abaissant le plancher

Plutôt que de deviner quelles API dépassent quelle version, on abaisse
`_WIN32_WINNT` et on laisse les en-têtes trancher. Le compilateur devient
l'auditeur.

```bash
cmake -S . -B build-xp -G Ninja \
      -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501"
```

| Plancher visé | Erreurs de compilation |
|---|---|
| `0x0601` Windows 7 | **0** |
| `0x0600` Windows Vista | **0** |
| `0x0501` Windows XP | **2** |

Les deux seules erreurs, à Windows XP :

```
src/app/recent_files.cpp:30  'SHGetKnownFolderPath' was not declared in this scope
src/platform/window.cpp:162  'SM_CXPADDEDBORDER' was not declared in this scope
```

- `SHGetKnownFolderPath` (localisation de `%APPDATA%`) est Vista+. Remplacement :
  `SHGetFolderPathW` + `CSIDL_APPDATA`, valable de Windows 2000 à Windows 11.
- `SM_CXPADDEDBORDER` est une métrique Vista+, utilisée pour calculer l'encart
  de cadre d'une fenêtre maximisée. XP n'a pas de bordure rembourrée : la valeur
  y vaut simplement 0.

**Le second n'était pas détectable par la table d'imports** — c'est une
constante, pas une fonction importée. La méthode par table d'imports, utilisée
seule, l'aurait laissé passer ; le test de compilation le sort en une commande.

Aucun des deux n'a été corrigé : troquer `SHGetKnownFolderPath` contre l'API
dépréciée pour une plateforme qu'on ne vise pas serait une régression payée
aujourd'hui pour un bénéfice hypothétique.

### Le reste de la table d'imports est ancien

| DLL | Point le plus récent | Plancher |
|---|---|---|
| USER32 | `GetWindowLongPtrW`, `TrackMouseEvent` | Windows 2000 |
| KERNEL32 | `AttachConsole` | Windows XP |
| GDI32 | `CreateDIBSection`, `BitBlt` | Windows 95 |
| OPENGL32 | `wglCreateContext` | Windows 95 |
| comdlg32 | `GetOpenFileNameW` | Windows 95 |
| SHELL32 | `SHAppBarMessage` | Windows 95 |

### Ce que la résolution dynamique a déjà protégé

`dwmapi.dll` **n'apparaît pas** dans la table d'imports, confirmant que le motif
tient : la seule API à version plancher élevée (`DwmSetWindowAttribute`, Vista+ /
`DWMWA_WINDOW_CORNER_PREFERENCE`, Windows 11) est chargée à la demande et échoue
en silence. La décision prise pour les coins carrés est donc réutilisable telle
quelle pour tout futur appel récent. `gdiplus.dll` figurait ici pour la même
raison ; il a depuis été retiré du projet et ne se charge plus du tout.

De même, le choix d'**OpenGL 1.1 strict** (pas de shader, pas de VBO) est déjà
le bon plancher pour ces cibles, et le repli GDI y devient une vraie voie
d'exécution plutôt qu'un filet de sécurité.

### Windows 7 / 8 — le code source est déjà prêt

Mesuré, pas supposé : à plancher Windows 7, le projet compile **sans une seule
erreur**, se lie, et produit un binaire de 786 Kio dont la **table d'imports est
identique** à celle du binaire normal. Le binaire obtenu a été exécuté et
fonctionne (splash, polices, contexte OpenGL, liste des récents).

Autrement dit : **aucune modification de code n'est nécessaire pour Windows 7 ou
8.** Le seul obstacle est le runtime C décrit plus haut — l'UCRT. Deux voies :

1. livrer le redistribuable UCRT (KB2999226 pour Windows 7 SP1 / 8 / 8.1) ;
2. compiler avec la variante **MinGW-w64 MSVCRT** au lieu d'UCRT.

La machine de développement n'a que la variante UCRT
(`x86_64-w64-mingw32`, `ucrt-posix-seh`) : tester la seconde voie demande
d'installer une seconde chaîne.

Ce que le compilateur ne peut pas dire, et qui demande une VM :
le comportement d'Aero Snap sous le DWM de Windows 7, et la qualité des pilotes
OpenGL d'époque — c'est là que le repli GDI cesse d'être un filet de sécurité
pour devenir une vraie voie d'exécution.

### Windows Vista / XP

Vista compile déjà à zéro erreur : comme Windows 7, il ne demande **aucun
changement de source**, seulement la question du runtime.

XP demande les deux correctifs listés plus haut, tous deux localisés et courts.
Mais l'UCRT y est rédhibitoire : **aucun redistribuable n'existe pour XP**,
donc le changement de chaîne de compilation n'est pas une option parmi d'autres,
c'est un préalable.

Le choix d'**OpenGL 1.1 strict**, fait en v0, se révèle être exactement le bon
plancher pour ces cibles.

### Linux — surface de portage mesurée

Compté sur les sources, pas estimé : `grep` des en-têtes Win32 et `wc` par
module.

| Module | Lignes | Fichiers couplés à l'OS | Verdict |
|---|---|---|---|
| `font/` | 725 | **0** | porte tel quel |
| `render/` | 1 054 | **0** | porte tel quel |
| `doc/` | 943 | **0** | porte tel quel |
| `compress/` | 569 | **0** | porte tel quel |
| `image/` | 303 | **0** | porte tel quel |
| `app/` | 1 888 | **0** | porte tel quel |
| `platform/` | 1 418 | 12 / 19 | à réécrire intégralement |
| **Total** | **6 900** | | |

**5 482 lignes — 79 % du code — ne contiennent aucun appel système.** Toute la
logique métier, le moteur de rendu et l'interface se compilent sur n'importe
quelle plateforme sans une ligne de changement ; il ne reste à réécrire que
`platform/`, dont c'est précisément le rôle.

### Le découplage de `app/` — mesuré avant/après

`app/` contenait 22 appels Win32 dispersés dans 6 fichiers. Ils sont désormais
tous derrière `platform/`, dont l'API publique ne porte aucun type système.

| | Avant | Après |
|---|---|---|
| Fichiers de `app/` couplés à l'OS | 6 / 15 | **0 / 15** |
| Lignes de `app/` | 2 029 | **1 888** |
| Part du code indépendante de l'OS | 46 % | **79 %** |

Ce qui a été créé pour absorber ces appels :

| Module | Rôle |
|---|---|
| `platform/unicode.*` | conversions UTF-8 ↔ UTF-16 |
| `platform/system.*` | chemins, répertoire de données, variables d'environnement, mise en sommeil |
| `platform/keys.h` | codes de touches neutres, sans `<windows.h>` |
| `platform/popup_window.*` | fenêtre sans chrome de l'écran de chargement |
| `platform/entry.*` | point d'entrée du processus |

`Window` a gagné `set_title`, `capture_mouse` / `release_mouse` /
`has_mouse_capture` et `sync_client_size`, qui remplacent des appels bruts
faits depuis `app/`.

**Bénéfice immédiat, indépendant de tout portage :** les conversions
UTF-8 ↔ UTF-16 existaient en **quatre copies privées identiques**
(`app/app.cpp`, `app/recent_files.cpp`, `platform/dialogs.cpp`,
`platform/image.cpp`). Il n'en reste qu'une.

**Cette limite est levée.** `platform/window.h`, `platform/backend.h`,
`platform/dialogs.h` et `platform/popup_window.h` exposaient encore `HWND` :
`app/` ne contenait plus aucun appel système, mais tirait `<windows.h>` de façon
transitive. La poignée est désormais opaque (`platform/native_handle.h`), aucun
en-tête public de `platform/` n'inclut `<windows.h>`, et les huit fichiers de
`app/` compilent avec un `<windows.h>` empoisonné. Voir *Architecture → Poignée
de fenêtre opaque* pour la méthode de vérification.

### Réponses proposées aux questions de la fiche

**X11 seul, ou X11 + Wayland ?** → **X11 d'abord.** Notre chrome a besoin de
déclencher déplacement et redimensionnement depuis le client : X11 offre
`_NET_WM_MOVERESIZE`, l'analogue direct de notre `HTCAPTION`, compris par tout
gestionnaire conforme EWMH. Wayland a l'équivalent (`xdg_toplevel.move` /
`.resize`), plus propre, mais n'a aucun équivalent au positionnement absolu et
le support des décorations côté client y varie fortement selon le bureau. Et
XWayland fait tourner une application X11 sur une session Wayland, donc un
binaire X11 couvre déjà les deux mondes.

**Bureau de référence ?** → tester **GNOME (Mutter) et KDE (KWin)**, ce sont eux
qui divergent le plus sur les décorations client. Le principe « vérifié par
mesure, pas à l'œil » impose de disposer d'une VM avant d'écrire le prompt.

**CRLF partout, ou adaptation par OS ?** → **lecteur universel, écrivain
local.** Le lecteur accepte déjà CRLF, LF et CR isolé : il est portable sans
changement. L'écrivain devrait suivre la convention locale, pour qu'un `.txt`
produit sous Linux paraisse natif aux outils Linux. Le `content.xml` d'un `.odt`
reste en LF dans tous les cas — c'est un format interne. Cela ne casse pas
`gw_doc_roundtrip` : le test compare les **documents**, pas les octets.

**PNG : bibliothèque système ou décodeur maison ?** → **réglé.** Le décodeur
maison est écrit et GDI+ est retiré ; `image/` est un module pur qui porte tel
quel. La question ne se pose plus pour le portage. Cf. *Décompression*.

---

## Hors périmètre

Windows 7/8 et Linux ne sont pas ciblés. Le code évite les API strictement
Win10+ sans raison et `platform/` reste le principal point de portage, mais
rien n'a été développé ni testé pour ces cibles.
