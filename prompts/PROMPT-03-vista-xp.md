# Prompt 3 — Support Windows Vista / Windows XP SP2+

> Prérequis : prompt 2 terminé. VM Vista et VM XP SP2 disponibles.
>
> **Question à poser avant de lancer ce prompt** : qui utilise encore XP pour de
> la bureautique ? L'ambition « tourne sur n'importe quoi » reste valide, mais
> c'est l'objectif le plus lointain de la feuille de route, et le coût est réel.
> À traiter seulement si le projet est encore actif et la demande justifiée.

## Plancher retenu

**XP SP2 minimum**, pas XP RTM. Le documenter dans le README.

## Bloqueur déjà identifié

`SHGetKnownFolderPath` (`src/platform/system.cpp:52`) est **Vista+** : sous XP,
l'appel échoue et la persistance des fichiers récents est cassée. Repli à
écrire : `SHGetFolderPathW(CSIDL_APPDATA)`, résolu dynamiquement depuis
`shell32.dll` — pas de lien statique vers l'API Vista. C'est exactement le type
d'« API introduite implicitement » que l'audit doit chercher ailleurs.

## Ce qui n'est PAS un problème — ne pas y passer de temps

- **PNG** : décodeur maison + inflate maison, aucune dépendance à GDI+
  (`CMakeLists.txt:62`). La question « GDI+ disponible sous XP SP2 ? » est sans
  objet pour ce projet.
- **OpenGL 1.1 strict** : le choix fait tôt (pas de shader, pas de VBO) tombe
  juste pour cette cible. Ne pas le remettre en cause.
- **`GetOpenFileNameW`** : fonctionne sous XP. L'apparence diffère (pas le style
  Explorer) — cosmétique, ne pas chercher à la corriger.

## Tâche

1. **Audit complet préalable**, avant toute écriture de code : toutes les API
   Win32 du projet, avec leur version minimale documentée. Sortie = un tableau
   API / version minimale / verdict / action. Ce travail précède le code, il ne
   se découvre pas en cours de route.
2. Traiter chaque API postérieure à XP par résolution dynamique + repli, selon
   le motif déjà utilisé pour `dwmapi`.
3. **Absence totale de DWM sous XP** : pas de composition. Vérifier que le
   chrome custom (`WM_NCCALCSIZE` → 0) rend correctement sous Luna et sous le
   thème classique, et que le double buffering est entièrement assuré par le
   projet, sans compter sur la composition du système.
4. Toolchain : viser `_WIN32_WINNT=0x0501`, vérifier que le MinGW utilisé
   produit un binaire qui démarre réellement sous XP SP2.
5. Repli GDI : sous XP c'est le chemin par défaut attendu, pas une exception.
   Le traiter et le tester comme tel.

## Vérification

- Exécution réelle sur VM XP SP2 et VM Vista. Sans ces VM, ce prompt ne peut
  pas être considéré comme terminé — l'adaptation documentaire seule ne prouve
  rien.
- Scénario complet sur chaque VM : splash, accueil, ouverture .txt et .odt,
  écriture d'un fichier récent puis relance pour confirmer la persistance
  (c'est le chemin réparé, il doit être vérifié explicitement), drag, resize,
  Aero Snap (Vista uniquement — absent sous XP), thème Luna et thème classique.
- Aucune régression sur Win7/8/10/11 : rejouer les scénarios des prompts
  précédents avec le même binaire.
