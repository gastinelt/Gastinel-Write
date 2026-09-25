// platform/unicode.h — conversions UTF-8 <-> UTF-16.
//
// Ces fonctions n existent que parce que l API Win32 est en UTF-16 alors que
// tout le reste du programme travaille en UTF-8. Elles n ont donc rien a faire
// ailleurs que dans platform/ : sur un systeme dont les appels systeme sont
// deja en UTF-8, elles deviennent l identite.
//
// Elles remplacent quatre copies privees identiques qui trainaient dans
// app/app.cpp, app/recent_files.cpp, platform/dialogs.cpp et
// platform/image.cpp.
#pragma once

#include <string>

namespace gw::platform {

std::wstring to_wide(const std::string& utf8);

std::string to_utf8(const std::wstring& wide);

// Variante pour les chaines rendues par l API systeme, terminees par un nul.
std::string to_utf8(const wchar_t* wide);

} // namespace gw::platform
