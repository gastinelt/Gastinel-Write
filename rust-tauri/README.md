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

## État actuel

Squelette minimal Tauri (v0, sans extensions) : fenêtre + zone de texte de base. À développer.
