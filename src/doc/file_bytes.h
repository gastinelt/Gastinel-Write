// doc/file_bytes.h — lecture/ecriture de fichiers en binaire.
//
// Toujours en binaire, jamais en mode texte : le mode texte du CRT Windows
// traduit les fins de ligne dans notre dos, ce qui rendrait impossible toute
// detection dencodage et casserait le round-trip dun .odt (donnees binaires).
#pragma once

#include <string>

namespace gw::doc {

bool read_file_binary(const std::string& path, std::string& out, std::string& error);
bool write_file_binary(const std::string& path, const std::string& data, std::string& error);

} // namespace gw::doc
