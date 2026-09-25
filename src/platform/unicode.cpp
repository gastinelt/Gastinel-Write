#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gw::platform {

std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};

    // On passe la longueur explicitement plutot que de compter sur un nul
    // terminal : une chaine C++ peut contenir des zeros, et -1 ferait alors
    // tronquer silencieusement.
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};

    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                        needed);
    return out;
}

std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};

    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                           static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                           nullptr);
    if (needed <= 0) return {};

    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(),
                        needed, nullptr, nullptr);
    return out;
}

std::string to_utf8(const wchar_t* wide) {
    if (wide == nullptr || wide[0] == L'\0') return {};
    return to_utf8(std::wstring(wide));
}

} // namespace gw::platform
