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

## État actuel

Squelette minimal Win32 + GDI : fenêtre + zone de texte via contrôle EDIT natif (le plus léger possible, pas de rendu de glyphes maison tant que le point "réutilisation du C++" n'est pas tranché). À développer une fois les points ci-dessus clarifiés — notamment le système cible précis, qui conditionne les API Win32 utilisables.
