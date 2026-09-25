// platform/log.h — journalisation minimale.
//
// Sortie double : OutputDebugStringA (visible dans DebugView / debuggeur) et
// une console attachée si le processus en a une. Aucune dependance externe.
#pragma once

#include <string>

namespace gw::platform {

// Ecrit une ligne de log formatee (style printf), terminee par un saut de ligne.
void log(const char* fmt, ...);

// Idem, prefixe "[ERREUR] ".
void log_error(const char* fmt, ...);

// Dernier message d'erreur enregistre via log_error.
std::string last_logged_error();

// Chemin complet vers le fichier journal sur disque (%APPDATA%\Gastinel\Write\gastinel_write.log).
std::string log_file_path();

// Rattache la console du processus parent, si le programme a ete lance depuis
// un terminal. Sans effet sinon.
void attach_parent_console();

} // namespace gw::platform
