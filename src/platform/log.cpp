#include "platform/log.h"
#include "platform/system.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gw::platform {
namespace {

std::string g_last_error;
bool g_first_write = true;

void write_to_file(const char* line) {
    const std::string path = log_file_path();
    if (path.empty()) return;

    FILE* f = nullptr;
    const char* mode = g_first_write ? "w" : "a";
    if (fopen_s(&f, path.c_str(), mode) == 0 && f != nullptr) {
        g_first_write = false;
        std::fputs(line, f);
        std::fclose(f);
    }
}

void emit(const char* prefix, const char* fmt, va_list args) {
    char body[1024];
    std::vsnprintf(body, sizeof(body), fmt, args);

    if (std::strcmp(prefix, "[ERREUR] ") == 0) {
        g_last_error = body;
    }

    char line[1152];
    std::snprintf(line, sizeof(line), "%s%s\n", prefix, body);

    OutputDebugStringA(line);
    std::fputs(line, stdout);
    std::fflush(stdout);

    write_to_file(line);
}

} // namespace

void attach_parent_console() {
    // Si la sortie standard a deja ete redirigee par l appelant (> fichier,
    // pipe), on ne touche a rien : rebrancher sur CONOUT$ ferait disparaitre
    // les journaux la ou l utilisateur les attend.
    if (GetStdHandle(STD_OUTPUT_HANDLE) != nullptr) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) return;

    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
}

void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit("", fmt, args);
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit("[ERREUR] ", fmt, args);
    va_end(args);
}

std::string last_logged_error() {
    return g_last_error;
}

std::string log_file_path() {
    static std::string path = []() {
        const std::string dir = user_data_directory("Gastinel", "Write");
        if (dir.empty()) return std::string();
        return dir + "\\gastinel_write.log";
    }();
    return path;
}

} // namespace gw::platform
