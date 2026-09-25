#include "platform/system.h"

#include "platform/unicode.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

namespace gw::platform {

std::string executable_directory() {
    // MAX_PATH suffit pour un chemin d installation normal. Un chemin plus
    // long tronquerait, mais GetModuleFileNameW le signale et on rend alors le
    // repertoire courant plutot qu un chemin faux.
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".";

    const std::string path = to_utf8(std::wstring(buffer, len));
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool directory_exists(const std::string& path) {
    const DWORD attrs = GetFileAttributesW(to_wide(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_exists(const std::string& path) {
    const DWORD attrs = GetFileAttributesW(to_wide(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool create_directories(const std::string& path) {
    if (path.empty()) return false;
    if (directory_exists(path)) return true;

    // CreateDirectory echoue si le parent manque : on remonte donc jusqu au
    // premier parent existant, puis on redescend.
    const std::size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos && slash > 0) {
        create_directories(path.substr(0, slash));
    }

    if (CreateDirectoryW(to_wide(path).c_str(), nullptr)) return true;
    // Une course entre deux creations concurrentes n est pas une erreur.
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::string user_data_directory(const std::string& vendor, const std::string& app) {
    wchar_t* roaming = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming) != S_OK ||
        roaming == nullptr) {
        return {};
    }
    const std::string base = to_utf8(roaming);
    CoTaskMemFree(roaming);
    if (base.empty()) return {};

    std::string dir = base;
    if (!vendor.empty()) dir += "\\" + vendor;
    if (!app.empty()) dir += "\\" + app;

    if (!create_directories(dir)) return {};
    return dir;
}

std::string system_fonts_directory() {
    wchar_t* fonts = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fonts) == S_OK && fonts != nullptr) {
        std::string path = to_utf8(fonts);
        CoTaskMemFree(fonts);
        return path;
    }
    const std::string win = environment_variable("SystemRoot");
    if (!win.empty()) return win + "\\Fonts";
    return "C:\\Windows\\Fonts";
}

bool delete_file(const std::string& path) {
    if (DeleteFileW(to_wide(path).c_str())) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

std::string environment_variable(const char* name) {
    // Deux appels : le premier donne la taille necessaire, le second remplit.
    // Une variable absente rend 0, qu on ne distingue pas d une variable vide —
    // c est sans importance pour nos usages, qui traitent les deux pareil.
    const DWORD needed = GetEnvironmentVariableA(name, nullptr, 0);
    if (needed == 0) return {};

    std::string value(static_cast<std::size_t>(needed), '\0');
    const DWORD written = GetEnvironmentVariableA(name, value.data(), needed);
    value.resize(written);
    return value;
}

void sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
    Sleep(static_cast<DWORD>(milliseconds));
}

void trim_working_set() {
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void enable_dpi_awareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
        auto set_ctx = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_ctx) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((HANDLE)-4)
            if (set_ctx(reinterpret_cast<HANDLE>(-4))) {
                return;
            }
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE = ((HANDLE)-3)
            if (set_ctx(reinterpret_cast<HANDLE>(-3))) {
                return;
            }
        }
    }

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessProc)(int);
        auto set_aware = reinterpret_cast<SetProcessDpiAwarenessProc>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (set_aware) {
            // PROCESS_PER_MONITOR_DPI_AWARE = 2
            if (SUCCEEDED(set_aware(2))) {
                FreeLibrary(shcore);
                return;
            }
        }
        FreeLibrary(shcore);
    }

    if (user32) {
        typedef BOOL (WINAPI *SetProcessDPIAwareProc)();
        auto set_dpi_aware = reinterpret_cast<SetProcessDPIAwareProc>(
            GetProcAddress(user32, "SetProcessDPIAware"));
        if (set_dpi_aware) {
            set_dpi_aware();
        }
    }
}

} // namespace gw::platform
