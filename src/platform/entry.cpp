#include "platform/entry.h"

#include "platform/log.h"
#include "platform/system.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Le binaire est lie en sous-systeme Windows (pas de console). Les journaux
// partent vers OutputDebugStringA dans tous les cas ; attach_parent_console les
// rend aussi lisibles quand le programme est lance depuis un terminal.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    gw::platform::enable_dpi_awareness();
    gw::platform::attach_parent_console();
    return gw::application_main();
}
