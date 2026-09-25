# Prompt 2 — Support Windows 7 / Windows 8

> Prérequis : socle Windows 10/11 stabilisé. Prérequis matériel : une VM Win7 et
> une VM Win8.1 réellement disponibles. Ne pas lancer ce prompt « à l'aveugle » :
> le travail consiste presque entièrement à vérifier des suppositions, et sans
> machine de test il n'y a rien à vérifier.

## Ce qui est déjà réglé — ne pas refaire

- `DWMWCP_DONOTROUND` : `DwmSetWindowAttribute` est déjà résolu dynamiquement
  par `GetProcAddress` (`src/platform/window.cpp:104`), avec échec silencieux si
  l'API est absente. Le comportement Win7/8 attendu est donc « coins non
  arrondis, pas de plantage ». À confirmer par exécution, mais aucun code à
  écrire a priori.
- **PNG** : décodeur maison, GDI+ n'est pas lié (`CMakeLists.txt:62`). Sans objet.

## Tâche

1. **Audit d'abord, code ensuite.** Recenser chaque API Win32 appelée dans
   `src/platform/` et vérifier sa version minimale dans la documentation
   Microsoft. Produire la liste avant toute modification. Le seul cas déjà
   identifié comme suspect est `SHGetKnownFolderPath`
   (`src/platform/system.cpp:52`) — Vista+, donc bon pour Win7/8, mais à traiter
   au prompt 3.
2. Pour chaque API postérieure à Windows 7 trouvée : résolution dynamique via
   `GetProcAddress` avec repli, sur le modèle déjà en place pour `dwmapi`.
   **Pas** de compilation conditionnelle : le projet vise un seul binaire
   multi-versions, cohérent avec la détection runtime OpenGL/GDI.
3. Vérifier le repli GDI sur des pilotes d'époque. Sous Win7/8 il n'est plus un
   filet de sécurité mais un chemin d'exécution probable : le tester comme un
   chemin principal.
4. Retester Aero Snap. Il existe depuis Win7, mais le comportement du DWM
   diffère — ne pas conclure de Win11 vers Win7.
5. Vérifier qu'aucun artefact visuel du thème Aero n'apparaît derrière la
   fenêtre sans décoration (`WM_NCCALCSIZE` renvoyant 0).

## Hors périmètre

- Le DPI par moniteur reste non géré, comme sous Win10/11. C'est un manque
  existant qui devient plus visible, pas une régression à corriger ici.

## Vérification

- Exécution réelle sur VM Win7 et VM Win8.1 : splash, accueil, ouverture .txt
  et .odt, drag, resize, Aero Snap, boutons de fenêtre.
- Chemin OpenGL et chemin GDI testés séparément sur chaque VM.
- Le binaire testé est le MÊME que celui qui tourne sous Win11 : le vérifier par
  empreinte, pas par « c'est le même build ».
- Aucune régression Win10/11 : rejouer le même scénario sur la cible principale.
