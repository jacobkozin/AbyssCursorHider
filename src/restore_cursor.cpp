#include "abyss/core.hpp"
#include "abyss/cursor_controller.hpp"

#include <shellapi.h>
#include <windows.h>

#include <string_view>

namespace
{

bool RunWatchdog(const DWORD process_id) noexcept
{
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (process != nullptr)
    {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }

    return abyss::RestoreSystemCursorScheme();
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);

    if (arguments != nullptr && argument_count == 3 &&
        std::wstring_view(arguments[1]) == L"--watch")
    {
        const DWORD process_id = static_cast<DWORD>(abyss::ParseProcessId(arguments[2]));
        LocalFree(arguments);
        if (process_id == 0)
        {
            return 2;
        }
        return RunWatchdog(process_id) ? 0 : 1;
    }

    if (arguments != nullptr)
    {
        LocalFree(arguments);
    }

    const bool restored = abyss::RestoreSystemCursorScheme();
    MessageBoxW(nullptr,
                restored ? L"Windows cursor scheme restored."
                         : L"Windows could not reload the cursor scheme.",
                L"Abyss Cursor Restore", MB_OK | (restored ? MB_ICONINFORMATION : MB_ICONERROR));
    return restored ? 0 : 1;
}
