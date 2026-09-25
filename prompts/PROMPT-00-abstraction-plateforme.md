# Prompt 0 — Extraire une couche d'abstraction plateforme (prérequis Linux)

> Préalable obligatoire aux prompts 1/2/3. À lancer une fois le socle Windows
> (paramètres, éditeur formaté, mode clavier-first) stabilisé.

## Contexte

Aujourd'hui il n'existe aucune frontière plateforme dans le projet :
`src/platform/backend.h` inclut `<windows.h>` et expose `init(HWND)`. Le type
Win32 traverse donc l'interface censée être « commune OpenGL et GDI ». Tant que
c'est le cas, tout portage revient à dupliquer l'arborescence.

Périmètre Win32 réel, mesuré : 10 fichiers contiennent `windows.h` ou une API
Win32 — `src/platform/{backend.h,dialogs.h,entry.cpp,gl_backend.cpp,keys.h,
log.cpp,system.cpp,system.h,unicode.cpp,window.h}`, ~1200 lignes au total.
Tout le reste (`render/`, `font/`, `doc/`, `image/`, `compress/`) est du C++ pur.

## Tâche

Introduire une frontière plateforme SANS ajouter de code Linux, et SANS changer
le comportement Windows.

1. Définir un handle de fenêtre opaque neutre (p. ex. `gw::platform::NativeWindow`)
   remplaçant `HWND` dans `backend.h`, `popup_window.h`, `window.h`. Aucun
   en-tête hors `src/platform/win32/` ne doit plus inclure `<windows.h>`.
2. Déplacer les implémentations Win32 sous `src/platform/win32/`, en gardant les
   en-têtes neutres à la racine de `src/platform/`. Le CMakeLists sélectionne le
   répertoire par `if(WIN32)`.
3. Isoler derrière l'interface neutre : création/boucle de fenêtre, backend de
   présentation, dialogues fichier, chemin de configuration persistante,
   journalisation, conversion UTF-8/UTF-16, codes de touches.
4. Ne PAS toucher au décodage PNG ni à l'inflate : `src/image/png.cpp` et
   `src/compress/inflate.cpp` sont du C++ portable et se compilent tels quels.
   (GDI+ n'est pas lié — voir CMakeLists.txt ligne 62.)

## Contraintes

- Refactoring à comportement constant : aucun changement visuel, aucun
  changement de fonctionnalité.
- Un seul binaire par plateforme, détection runtime conservée pour OpenGL/GDI.
- OpenGL 1.1 strict : pas de shader, pas de VBO. Ne pas « moderniser ».
- Pas de dépendance tierce ajoutée.

## Vérification (par mesure, pas à l'œil)

- Le build Windows passe, `gw_doc_roundtrip` et `gw_compress_tests` passent.
- `grep -rl "windows.h" src --include=*.h` ne retourne plus que des fichiers
  sous `src/platform/win32/`.
- Lancement manuel : splash, accueil, ouverture d'un .txt et d'un .odt, drag de
  la fenêtre par la barre custom, resize par les bords, Aero Snap, boutons
  minimiser/maximiser/fermer. Comparer à un run de la version d'avant sur le
  même document, pas à un souvenir.
