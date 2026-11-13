#include "abyss/cursor_controller.hpp"

#include <windows.h>

// Manual recovery tool: double-click it to reload your Windows cursor scheme.
// The automatic watchdog is a second copy of AbyssCursorHide.exe (see
// TryRunWatchdogMode in main.cpp), so this program never runs in the background.
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    // Load DLLs only from System32, never from this program's own folder.
    (void)SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    const bool restored = abyss::RestoreSystemCursorScheme(abyss::kAllCursorRoles);
    MessageBoxW(nullptr,
                restored ? L"Windows cursor scheme restored."
                         : L"Some pointer shapes still look invisible after reloading the "
                           L"cursor scheme. Signing out and back in restores them.",
                L"Abyss Cursor Restore", MB_OK | (restored ? MB_ICONINFORMATION : MB_ICONERROR));
    return restored ? 0 : 1;
}
