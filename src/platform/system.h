// platform/system.h — services systeme non graphiques.
//
// Chemins, variables d environnement, mise en sommeil. Tout ce que la couche
// applicative demandait jusqu ici directement a Win32, et qui l empechait de
// se compiler ailleurs.
//
// Les signatures ne portent aucun type Win32 : c est ce qui permet a `app/` de
// les appeler sans inclure <windows.h>.
#pragma once

#include <string>

namespace gw::platform {

// Repertoire contenant l executable courant, sans separateur final.
//
// C est la seule racine fiable pour trouver les assets : le repertoire de
// travail du processus, lui, peut etre n importe quoi — et le selecteur de
// fichier systeme est reputé le changer.
std::string executable_directory();

bool directory_exists(const std::string& path);

bool file_exists(const std::string& path);

// Cree le repertoire et tous ses parents manquants.
// Retourne vrai si le repertoire existe a la sortie, y compris s il existait
// deja.
bool create_directories(const std::string& path);

// Repertoire de donnees utilisateur pour <vendor>/<app>, cree au besoin.
// Retourne une chaine vide si l emplacement systeme est introuvable.
//
// Volontairement pas a cote de l executable : celui-ci peut etre installe dans
// un repertoire en lecture seule.
std::string user_data_directory(const std::string& vendor, const std::string& app);

// Repertoire des polices du systeme (ex. C:\Windows\Fonts).
std::string system_fonts_directory();

// Supprime un fichier. Retourne vrai s il n existe plus a la sortie, y
// compris s il n existait deja pas.
bool delete_file(const std::string& path);

// Valeur d une variable d environnement, vide si absente.
std::string environment_variable(const char* name);

void sleep_ms(int milliseconds);

// Libere les pages physiques inutilisees vers le systeme (Working Set).
void trim_working_set();

// Active la prise en charge Per-Monitor DPI Awareness V2.
void enable_dpi_awareness();

} // namespace gw::platform
