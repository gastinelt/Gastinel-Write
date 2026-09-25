# Gastinel Write — Rust/Tauri

Déclinaison pour PC modernes (≥100 Mo de ressources disponibles au lancement). Nouveau projet, ne réécrit pas l'architecture C++ ligne à ligne, mais reste fidèle à l'identité du produit (nom, slogan, DA).

DA de référence : celle de Gastinel Weather Studio (Inter, minimalisme, précision géométrique, bleu en accentuation fonctionnelle uniquement).

## Décisions tranchées avec Théo

- **Rendu de texte** : WebView, HTML/CSS/JS vanilla — pas de framework front externe (pas de React/Vue/etc.), pas de rendu maison.
- **Extensions** : JS/TS exécuté dans le contexte WebView (pas de plugins Rust compilés séparément).
- **Périmètre v0** : éditeur de base (texte + formatage) d'abord, sans le squelette d'extensions actif — recommandation du document de cadrage, non contestée par Théo. *(À reconfirmer explicitement si besoin.)*
- **IA future (Gastinel Mind)** : l'architecture d'extensions reste généraliste, ne présume pas de ce besoin.

## Encore à trancher avec Théo (non répondu pour l'instant)

- Nom exact du dossier/paquet (`gastinel-write-rust` ou autre) — actuellement `rust-tauri/`, provisoire.
- Format de fichier : reprise du `.txt`/`.odt` du C++ pour la cohérence produit, ou format propre à cette version ?
- Format de manifeste/sandbox des extensions (accès document ouvert ? fichiers ? réseau ?) — nécessaire avant la première extension réelle (prise de notes IA), pas avant.
- Licence et visibilité du dossier (mêmes règles "tous droits réservés" que le C++ ?).

## État actuel (v0)

Éditeur de texte brut fonctionnel, compilé et testé sous Linux (Xvfb).

- Nouveau / Ouvrir / Enregistrer / Enregistrer sous (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Maj+S).
- Demande de confirmation si le document est modifié (nouveau, ouvrir, fermeture de fenêtre).
- Lecture UTF-8 (BOM ignoré), repli Latin-1 pour les vieux fichiers `.txt`.
- Écriture atomique (fichier temporaire + renommage) : jamais de document à moitié écrit.
- Compteur mots/caractères, titre de fenêtre avec indicateur de modification, thème sombre automatique.
- Ouverture d'un fichier passé en argument de la ligne de commande.

Choix provisoires (à valider) : `.txt` uniquement tant que le format n'est pas tranché ; `<textarea>` plutôt que `contenteditable` (plus rapide sur les gros textes, fidèle au texte brut) — le formatage attendra la décision sur le format ; police Inter utilisée si installée, à embarquer localement (licence OFL) plus tard ; icône provisoire.

## Optimisations

- Profil release : `opt-level = "s"`, LTO, `codegen-units = 1`, `panic = "abort"`, symboles retirés → binaire de 4,5 Mo.
- Une seule dépendance en plus de Tauri : `tauri-plugin-dialog` (officiel). Aucun `node_modules`, aucun bundler.
- Droits de la WebView réduits au strict nécessaire (`capabilities/default.json`) et CSP stricte.
- Compteur de mots calculé en période d'inactivité, pas à chaque frappe.

## Commandes

```
cargo run                 # lancer en développement
cargo test                # tests (lecture/écriture de fichiers)
cargo build --release     # binaire optimisé
cargo install tauri-cli --version "^2" && cargo tauri build   # installeurs (.msi, .deb, ...)
```

Dépendances système sous Linux : `libwebkit2gtk-4.1-dev libgtk-3-dev librsvg2-dev`.
