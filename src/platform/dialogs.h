// platform/dialogs.h — boites de dialogue systeme.
//
// On utilise les dialogues natifs de Windows plutot que den redessiner un
// dans la charte Gastinel. Deux raisons : un selecteur de fichier maison
// devrait reimplementer la navigation, les raccourcis, les emplacements
// reseau et laccessibilite ; et lutilisateur attend precisement le dialogue
// systeme a cet endroit. Le chrome custom sarrete a la fenetre de
// lapplication.
#pragma once

#include <string>

#include "platform/native_handle.h"

namespace gw::platform {

// Ouvre le selecteur de fichier, filtre sur les formats geres.
// Retourne false si lutilisateur annule. Le chemin rendu est en UTF-8.
bool prompt_open_document(NativeHandle owner, std::string& path_out);

// Selecteur d enregistrement. `suggested_name` prerempli le champ du nom.
//
// Le dialogue ajoute lui-meme l extension par defaut si l utilisateur n en
// tape pas : sans cela, un nom saisi sans extension produirait un fichier sans
// extension, que l application relirait ensuite comme du texte brut alors que
// l utilisateur croyait enregistrer un ODT.
bool prompt_save_document(NativeHandle owner, const std::string& suggested_name,
                          std::string& path_out);

// Question a trois reponses posee avant d abandonner un document modifie.
enum class ConfirmResult {
    Yes,
    No,
    Cancel,
};

// Boite « enregistrer les modifications ? ». C est le seul endroit ou une
// boite systeme s impose : elle doit rester au premier plan et bloquer, y
// compris pendant une fermeture de fenetre, ce qu un rectangle dessine dans
// notre propre framebuffer ne peut pas faire tant que la boucle de messages
// continue de tourner.
ConfirmResult prompt_discard_changes(NativeHandle owner, const std::string& document_name);

// Selecteur de fichier de police, filtre sur .ttf.
//
// LIMITE ASSUMEE : le filtre ne propose pas .otf. Le parser ne lit que des
// contours `glyf` ; un .otf porte le plus souvent des contours CFF, qu il
// refuserait. Proposer une extension qui echoue neuf fois sur dix serait une
// fausse fonctionnalite. Un .otf a contours TrueType existe et reste
// selectionnable via le filtre "Tous les fichiers".
bool prompt_open_font(NativeHandle owner, std::string& path_out);

// Boite de dialogue d erreur bloquante, pour les echecs qui empechent le
// programme de demarrer — a ce moment-la, il n y a pas encore de fenetre pour
// afficher quoi que ce soit nous-memes.
void show_error_message(const std::string& title, const std::string& text);

} // namespace gw::platform
