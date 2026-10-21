#include "abyss/cursor_controller.hpp"

#include <windows.h>

// Manual recovery tool: double-click it to reload your Windows cursor scheme.
// The automatic watchdog is a second copy of AbyssCursorHide.exe (see
// TryRunWatchdogMode in main.cpp), so this program never runs in the background.
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    const bool restored = abyss::RestoreSystemCursorScheme();
    MessageBoxW(nullptr,
                restored ? L"Windows cursor scheme restored."
                         : L"Windows could not reload the cursor scheme.",
                L"Abyss Cursor Restore", MB_OK | (restored ? MB_ICONINFORMATION : MB_ICONERROR));
    return restored ? 0 : 1;
}
