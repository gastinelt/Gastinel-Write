# Prompt 1 — Portage Linux (X11)

> Prérequis : le prompt 0 (abstraction plateforme) doit être terminé et fusionné.

## Décisions déjà tranchées (ne pas rouvrir en cours de route)

- **X11 uniquement** pour cette passe. Wayland est hors périmètre : il interdit
  au client de déplacer sa propre fenêtre comme le fait le chrome custom
  Windows, ce qui demande une conception séparée. Ne pas écrire de code Wayland
  « au cas où ».
- **Distribution de référence** : Debian/Ubuntu récente sous GNOME. Les autres
  environnements ne sont pas testés dans cette passe ; documenter ce fait.
- **Fins de ligne** : LF à l'écriture sous Linux, mais la lecture accepte CRLF
  et LF indifféremment sur les deux OS (déjà le cas côté `doc/`, à vérifier).
- **PNG** : rien à faire. Le décodeur est maison (`src/image/png.cpp` +
  `src/compress/inflate.cpp`), sans GDI+ ni libpng. Ne pas introduire libpng.

## Tâche

Implémenter le backend Linux/X11 derrière les interfaces neutres du prompt 0,
sous `src/platform/x11/`.

1. **Fenêtrage** : `XOpenDisplay`/`XCreateWindow` + boucle d'événements, en
   remplacement de la boucle de messages Win32.
2. **Chrome custom** : désactiver la décoration serveur via `_MOTIF_WM_HINTS`,
   puis réimplémenter drag et resize côté client — même logique de zones que
   `WM_NCHITTEST` dans `src/platform/window.cpp`, mais pilotée par les
   événements souris X11. Utiliser `_NET_WM_MOVERESIZE` quand le gestionnaire
   de fenêtres l'annonce, avec repli sur un déplacement manuel sinon.
3. **Contexte graphique** : GLX pour OpenGL 1.1. Le repli `gdi_backend` n'a pas
   d'équivalent : écrire un repli logiciel qui blitte le `Framebuffer` CPU via
   `XPutImage` sur une `XImage`. Conserver la détection runtime : un binaire,
   GLX d'abord, `XPutImage` sinon.
4. **Dialogues fichier** : `xdg-desktop-portal` via D-Bus si le portail répond,
   sinon un sélecteur maison minimal dessiné avec le rasterizer existant. Ne
   PAS lier GTK ni Qt.
5. **Persistance** : `$XDG_CONFIG_HOME/gastinel/write/`, avec repli
   `~/.config/gastinel/write/`. Format de fichier identique à Windows.
6. **Clavier** : mapper les keysyms X11 vers `src/platform/keys.h`, en couvrant
   au minimum ce que le mode clavier-first utilise.

## Contraintes

- Aucune dépendance tierce : Xlib, GLX et libdbus sont des bibliothèques
  système, tout le reste est écrit dans le projet.
- OpenGL 1.1 strict : pas de shader, pas de VBO.
- Ne modifier aucun fichier sous `src/platform/win32/`. Si un changement y
  semble nécessaire, c'est que l'abstraction du prompt 0 est incomplète :
  s'arrêter et le signaler plutôt que de contourner.
- `render/`, `font/`, `doc/`, `image/`, `compress/` ne doivent recevoir aucune
  modification liée à la plateforme.

## Vérification

- Le build Windows continue de passer, inchangé, après le travail Linux.
- Build Linux + `gw_doc_roundtrip` + `gw_compress_tests` passent.
- Test manuel sur la distribution de référence : splash, accueil, ouverture
  .txt et .odt, drag, resize par chaque bord et chaque coin, maximiser,
  raccourcis clavier.
- Test explicite du repli logiciel, en forçant l'échec GLX (variable
  d'environnement dédiée ou VM sans accélération). Le repli doit être vérifié,
  pas supposé.
- Un même .odt ouvert sous Windows et sous Linux doit produire le même texte
  rendu ; comparer sur un document réel, pas sur une capture d'écran à l'œil.
