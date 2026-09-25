// app/main.cpp — demarrage de l application.
//
// Ce fichier ne contient aucun appel Win32 : le point d entree reel du systeme
// vit dans platform/entry.cpp, qui appelle simplement gw::application_main().
#include "app/app.h"
#include "platform/dialogs.h"
#include "platform/entry.h"
#include "platform/log.h"

#include <string>

namespace gw {

int application_main() {
    platform::log("Gastinel Write — demarrage");

    app::App application;
    if (!application.init()) {
        platform::log_error("Initialisation impossible, arret.");
        // A ce stade il n y a pas encore de fenetre : sans cette boite, un
        // lancement depuis l explorateur echouerait en silence complet.
        const std::string err = platform::last_logged_error();
        const std::string log_path = platform::log_file_path();
        std::string msg = "Gastinel Write n'a pas pu démarrer.\n\n";
        if (!err.empty()) {
            msg += "Détail : " + err + "\n\n";
        }
        if (!log_path.empty()) {
            msg += "Consultez le fichier journal pour plus d'informations :\n" + log_path;
        }
        platform::show_error_message("Gastinel Write — Erreur de démarrage", msg);
        return 1;
    }
    return application.run();
}

} // namespace gw
