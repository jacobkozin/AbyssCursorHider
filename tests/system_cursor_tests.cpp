// Tests that hide and restore this machine's REAL system cursors, thousands of
// times, and run the app's --restore and --watch modes. They are only built
// with -DABYSS_SYSTEM_CURSOR_TESTS=ON (CI turns it on) and only run when
// ABYSS_SYSTEM_CURSOR_TESTS=1 is set on GitHub Actions, whose runners are
// thrown away after each job (or with ABYSS_SYSTEM_CURSOR_TESTS_LOCAL=1, for a
// throwaway virtual machine). They also skip when this session cannot change
// system cursors. Skips exit with 77, which CTest reports as skipped.
//
// Usage: abyss_system_cursor_tests <path to AbyssCursorHide.exe>
#include "abyss/cursor_controller.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace
{

constexpr int kSkip = 77;
constexpr int kWarmupCycles = 20;
constexpr int kCycles = 2000;
// A scheme reload may keep a few shared cursors; a leak grows with every cycle.
constexpr long long kTolerance = 8;
constexpr int kMaxReportedFailures = 25;
constexpr DWORD kNormalRole = 32512;
constexpr DWORD kHandRole = 32649;

int g_failures = 0;

void Fail(const char* message, const long long a = 0, const long long b = 0)
{
    ++g_failures;
    if (g_failures <= kMaxReportedFailures)
    {
        std::printf("FAIL: %s (%lld, %lld)\n", message, a, b);
    }
}

long long UserObjects()
{
    return static_cast<long long>(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
}

long long GdiObjects()
{
    return static_cast<long long>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
}

void ReloadScheme()
{
    (void)SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
}

LONG WINAPI ReloadOnCrash(EXCEPTION_POINTERS*)
{
    ReloadScheme();
    return EXCEPTION_CONTINUE_SEARCH;
}

bool EnvironmentIs(const wchar_t* name, const wchar_t* expected)
{
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(name, value, ARRAYSIZE(value));
    return length > 0 && length < ARRAYSIZE(value) && std::wcscmp(value, expected) == 0;
}

struct Role
{
    DWORD id;
    const char* name;
    bool target;   // replaced by CursorController::Hide
    bool required; // Hide fails if Windows rejects it
};

// The 16 roles the app replaces, plus the two obsolete roles 5.0.0 also used.
constexpr std::array<Role, 18> kRoles = {{
    {32512, "NORMAL", true, true},       {32513, "IBEAM", true, true},
    {32514, "WAIT", true, true},         {32515, "CROSS", true, true},
    {32516, "UP", true, true},           {32640, "SIZE (old)", false, false},
    {32641, "ICON (old)", false, false}, {32642, "SIZENWSE", true, true},
    {32643, "SIZENESW", true, true},     {32644, "SIZEWE", true, true},
    {32645, "SIZENS", true, true},       {32646, "SIZEALL", true, true},
    {32648, "NO", true, true},           {32649, "HAND", true, true},
    {32650, "APPSTARTING", true, true},  {32651, "HELP", true, true},
    {32671, "PIN", true, false},         {32672, "PERSON", true, false},
}};

HCURSOR SystemCursor(const DWORD id)
{
    return LoadCursorW(nullptr, MAKEINTRESOURCEW(id));
}

HCURSOR MakeTransparentCursor(const int width, const int height)
{
    const auto stride = static_cast<std::size_t>(((width + 15) / 16) * 2);
    const std::vector<BYTE> and_mask(stride * static_cast<std::size_t>(height), 0xFF);
    const std::vector<BYTE> xor_mask(stride * static_cast<std::size_t>(height), 0x00);
    return CreateCursor(GetModuleHandleW(nullptr), 0, 0, width, height, and_mask.data(),
                        xor_mask.data());
}

enum class Rendered
{
    visible,
    invisible,
    error,
};

// Draws the cursor on black and on white; it is visible if either canvas
// changes. This does not use GetIconInfo, so it checks the app's own
// inspection code independently.
Rendered RenderCursor(const HCURSOR cursor)
{
    if (cursor == nullptr)
    {
        return Rendered::error;
    }

    constexpr int kCanvas = 256;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = kCanvas;
    info.bmiHeader.biHeight = -kCanvas;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
    {
        return Rendered::error;
    }
    void* pixels = nullptr;
    const HBITMAP canvas = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (canvas == nullptr || pixels == nullptr)
    {
        DeleteDC(dc);
        return Rendered::error;
    }
    const HGDIOBJ previous = SelectObject(dc, canvas);

    auto* const words = static_cast<std::uint32_t*>(pixels);
    const std::size_t count = static_cast<std::size_t>(kCanvas) * kCanvas;
    constexpr std::array<std::uint32_t, 2> kBackgrounds = {0x00000000U, 0x00FFFFFFU};

    Rendered result = Rendered::invisible;
    for (const std::uint32_t background : kBackgrounds)
    {
        std::fill(words, words + count, background);
        GdiFlush();
        if (DrawIconEx(dc, 0, 0, cursor, 0, 0, 0, nullptr, DI_NORMAL) == FALSE)
        {
            result = Rendered::error;
            break;
        }
        GdiFlush();
        const bool changed = std::any_of(words, words + count, [background](const std::uint32_t word) {
            return (word & 0x00FFFFFFU) != background;
        });
        if (changed)
        {
            result = Rendered::visible;
            break;
        }
    }

    SelectObject(dc, previous);
    DeleteObject(canvas);
    DeleteDC(dc);
    return result;
}

enum class AllRoles
{
    visible,
    some_invisible,
    unknown,
};

AllRoles RenderTargetRoles(const bool report)
{
    AllRoles result = AllRoles::visible;
    for (const Role& role : kRoles)
    {
        const HCURSOR cursor = role.target ? SystemCursor(role.id) : nullptr;
        if (cursor == nullptr)
        {
            continue;
        }
        const Rendered rendered = RenderCursor(cursor);
        if (rendered == Rendered::invisible)
        {
            result = AllRoles::some_invisible;
        }
        else if (rendered == Rendered::error && result == AllRoles::visible)
        {
            result = AllRoles::unknown;
        }
        if (report && rendered != Rendered::visible)
        {
            std::printf("    %s renders %s\n", role.name,
                        rendered == Rendered::invisible ? "transparent" : "with an error");
        }
    }
    return result;
}

void PrintSession()
{
    DWORD session = 0;
    (void)ProcessIdToSessionId(GetCurrentProcessId(), &session);
    USEROBJECTFLAGS flags{};
    DWORD needed = 0;
    const bool have_flags = GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS,
                                                      &flags, sizeof(flags), &needed) != FALSE;
    const HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    std::printf("session %lu, window station %s, input desktop %s, cursor size %dx%d\n", session,
                !have_flags                          ? "unknown"
                : (flags.dwFlags & WSF_VISIBLE) != 0 ? "interactive"
                                                     : "non-interactive",
                input != nullptr ? "accessible" : "not accessible", GetSystemMetrics(SM_CXCURSOR),
                GetSystemMetrics(SM_CYCURSOR));
    if (input != nullptr)
    {
        CloseDesktop(input);
    }
}

// Returns 0 when this session can change and reload system cursors, kSkip
// when it cannot, and 1 when it can change them but not reload them.
int ProbeSession(const int width, const int height)
{
    const HCURSOR cursor = MakeTransparentCursor(width, height);
    if (cursor == nullptr)
    {
        std::printf("SKIP: CreateCursor failed with error %lu.\n", GetLastError());
        return kSkip;
    }
    if (SetSystemCursor(cursor, kNormalRole) == FALSE)
    {
        const DWORD error = GetLastError();
        DestroyCursor(cursor);
        std::printf("SKIP: SetSystemCursor failed with error %lu; this session cannot change "
                    "system cursors (1459 means a non-interactive window station).\n",
                    error);
        return kSkip;
    }
    if (RenderCursor(SystemCursor(kNormalRole)) != Rendered::invisible)
    {
        ReloadScheme();
        std::printf("FAIL: SetSystemCursor reported success but the arrow did not change.\n");
        return 1;
    }
    if (SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0) == FALSE)
    {
        std::printf("FAIL: SPI_SETCURSORS failed with error %lu.\n", GetLastError());
        return 1;
    }
    if (RenderCursor(SystemCursor(kNormalRole)) != Rendered::visible)
    {
        std::printf("FAIL: SPI_SETCURSORS left the arrow transparent.\n");
        return 1;
    }
    return 0;
}

// Sets each role three times, the way three hides would, and reports the USER
// object count after each call relative to before the first. A cursor Windows
// takes leaves 0 0 0; one that stays charged to this process until the next
// replacement leaves 1 1 1, which is still flat; one Windows accepts but never
// frees leaves 1 2 3, a leak on every hide. Rejected cursors are destroyed
// here, as ReplaceWithTransparentCursor does, so a rejected role must not grow.
// The two obsolete roles are only reported; they show what 5.0.0 ran into.
void ProbeRoles(const int width, const int height)
{
    std::printf("Roles (USER objects after each of 3 SetSystemCursor calls):\n");
    const long long start = UserObjects();
    for (const Role& role : kRoles)
    {
        const long long before = UserObjects();
        std::array<long long, 3> held{};
        bool accepted = false;
        DWORD error = ERROR_SUCCESS;
        for (long long& count : held)
        {
            const HCURSOR cursor = MakeTransparentCursor(width, height);
            if (cursor == nullptr)
            {
                Fail("CreateCursor failed in the role probe", role.id);
                break;
            }
            SetLastError(ERROR_SUCCESS);
            accepted = SetSystemCursor(cursor, role.id) != FALSE;
            error = GetLastError();
            if (!accepted)
            {
                DestroyCursor(cursor);
            }
            count = UserObjects() - before;
        }
        const long long growth = held[2] - held[0];
        std::printf("  %5lu %-12s %s, error %lu, USER %+lld %+lld %+lld%s\n", role.id, role.name,
                    accepted ? "accepted" : "rejected", error, held[0], held[1], held[2],
                    accepted && growth != 0 ? "  <- accepted but never freed: leaks per hide"
                                            : "");
        if (!accepted && role.required)
        {
            Fail("Windows rejected a required role", role.id, error);
        }
        if (role.target && growth != 0)
        {
            Fail("a replaced role grows the USER object count", role.id, growth);
        }
    }
    ReloadScheme();
    std::printf("Roles: USER change after the reload %+lld\n", UserObjects() - start);
}

void StressHideRestore()
{
    abyss::CursorController controller(GetModuleHandleW(nullptr));
    for (int cycle = 0; cycle < kWarmupCycles; ++cycle)
    {
        (void)controller.Hide();
        (void)controller.Restore();
    }

    const long long user_before = UserObjects();
    const long long gdi_before = GdiObjects();
    std::printf("Stress: %d cycles, USER %lld, GDI %lld before\n", kCycles, user_before,
                gdi_before);

    for (int cycle = 1; cycle <= kCycles && g_failures < kMaxReportedFailures; ++cycle)
    {
        if (!controller.Hide())
        {
            Fail("Hide() failed", cycle, UserObjects());
            ReloadScheme();
            continue;
        }
        for (const Role& role : kRoles)
        {
            const HCURSOR cursor = role.target ? SystemCursor(role.id) : nullptr;
            if (cursor != nullptr && !abyss::IsTransparentCursor(cursor))
            {
                Fail("a replaced role is not transparent while hidden", cycle, role.id);
            }
        }
        if (abyss::CursorRolesVisible(abyss::kAllCursorRoles))
        {
            Fail("CursorRolesVisible() says everything is visible while hidden", cycle);
        }
        if (cycle % 500 == 0 && RenderCursor(SystemCursor(kNormalRole)) != Rendered::invisible)
        {
            Fail("the hidden arrow still draws something", cycle);
        }

        if (!controller.Restore())
        {
            Fail("Restore() reported a transparent cursor", cycle, UserObjects());
        }
        if (controller.hidden())
        {
            Fail("the controller still counts as hidden after Restore()", cycle);
        }
        if (cycle % 500 == 0)
        {
            if (RenderTargetRoles(true) != AllRoles::visible)
            {
                Fail("a restored cursor does not draw", cycle);
            }
            std::printf("  cycle %d: USER %lld, GDI %lld\n", cycle, UserObjects(), GdiObjects());
        }
    }

    const long long user_change = UserObjects() - user_before;
    const long long gdi_change = GdiObjects() - gdi_before;
    std::printf("Stress: USER change %+lld, GDI change %+lld\n", user_change, gdi_change);
    if (user_change > kTolerance)
    {
        Fail("USER objects grew across hide/restore cycles", user_change, kCycles);
    }
    if (gdi_change > kTolerance)
    {
        Fail("GDI objects grew across hide/restore cycles", gdi_change, kCycles);
    }
    if (!abyss::CursorRolesVisible(abyss::kAllCursorRoles))
    {
        Fail("a cursor reads as invisible after the last Restore()");
    }
}

// Starts AbyssCursorHide.exe with the given arguments. Returns the process
// handle, or nullptr.
HANDLE StartApp(const std::wstring& application, const std::wstring& arguments)
{
    std::wstring command = L"\"" + application + L"\" " + arguments;
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(application.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &startup, &process) == FALSE)
    {
        return nullptr;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

// Waits for a process to exit and returns its exit code, or -1 on a timeout.
long long WaitForExit(const HANDLE process, const DWORD timeout_ms)
{
    DWORD exit_code = STILL_ACTIVE;
    if (WaitForSingleObject(process, timeout_ms) == WAIT_OBJECT_0)
    {
        (void)GetExitCodeProcess(process, &exit_code);
    }
    return exit_code == STILL_ACTIVE ? -1 : static_cast<long long>(exit_code);
}

long long RunRestoreCopy(const std::wstring& application, const abyss::CursorRoleMask roles)
{
    const HANDLE process = StartApp(application, L"--restore " + std::to_wstring(roles));
    if (process == nullptr)
    {
        return -1;
    }
    const long long exit_code = WaitForExit(process, 15000);
    CloseHandle(process);
    return exit_code;
}

// Fills this process's USER object quota, as 5.0.0's leak eventually did, and
// checks that Restore() reports the failure, that Hide() refuses to run so
// close to the quota, and that a fresh --restore copy brings everything back.
void QuotaPhase(const std::wstring& application)
{
    abyss::CursorController controller(GetModuleHandleW(nullptr));
    constexpr std::array<std::size_t, 5> kHeadroom = {0, 2, 8, 32, 128};
    constexpr std::array<BYTE, 32> kAnd = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    constexpr std::array<BYTE, 32> kXor{};

    for (const std::size_t headroom : kHeadroom)
    {
        if (!controller.Hide())
        {
            Fail("Hide() failed before filling the quota", static_cast<long long>(headroom));
            ReloadScheme();
            return;
        }

        std::vector<HCURSOR> filler;
        filler.reserve(20000);
        DWORD fill_error = ERROR_SUCCESS;
        for (int index = 0; index < 20000; ++index)
        {
            const HCURSOR cursor = CreateCursor(nullptr, 0, 0, 16, 16, kAnd.data(), kXor.data());
            if (cursor == nullptr)
            {
                fill_error = GetLastError();
                break;
            }
            filler.push_back(cursor);
        }
        for (std::size_t freed = 0; freed < headroom && !filler.empty(); ++freed)
        {
            DestroyCursor(filler.back());
            filler.pop_back();
        }

        const bool reported = controller.Restore();
        const AllRoles rendered = RenderTargetRoles(true);
        std::printf("Quota: %zu fillers (error %lu), headroom %zu: Restore() %s, cursors %s\n",
                    filler.size(), fill_error, headroom, reported ? "true" : "false",
                    rendered == AllRoles::visible          ? "all draw"
                    : rendered == AllRoles::some_invisible ? "SOME TRANSPARENT"
                                                           : "unknown");
        if (reported && rendered == AllRoles::some_invisible)
        {
            Fail("Restore() reported success while a cursor is still transparent",
                 static_cast<long long>(headroom));
        }

        if (controller.Hide())
        {
            Fail("Hide() ran with the USER object quota nearly used up",
                 static_cast<long long>(headroom));
            (void)controller.Restore();
        }

        if (rendered == AllRoles::some_invisible)
        {
            const long long exit_code = RunRestoreCopy(application, controller.expected_roles());
            const AllRoles after = RenderTargetRoles(true);
            std::printf("Quota: --restore copy exited with %lld, cursors %s\n", exit_code,
                        after == AllRoles::visible ? "all draw" : "NOT ALL DRAW");
            if (exit_code != 0 || after != AllRoles::visible)
            {
                Fail("a fresh --restore copy did not bring the cursors back", exit_code,
                     static_cast<long long>(headroom));
            }
        }

        for (const HCURSOR cursor : filler)
        {
            DestroyCursor(cursor);
        }
        ReloadScheme();
        if (RenderTargetRoles(true) != AllRoles::visible)
        {
            Fail("cursors are still transparent after freeing the quota",
                 static_cast<long long>(headroom));
        }
    }
}

// The watchdog must wait while the process it watches runs, and reload the
// scheme as soon as that process is killed.
void WatchdogPhase(const std::wstring& application)
{
    wchar_t system_directory[MAX_PATH]{};
    if (GetSystemDirectoryW(system_directory, MAX_PATH) == 0)
    {
        Fail("GetSystemDirectoryW failed");
        return;
    }
    const std::wstring cmd = std::wstring(system_directory) + L"\\cmd.exe";
    std::wstring command = L"\"" + cmd + L"\" /c exit";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    // A suspended process stands in for the tray app.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION watched{};
    if (CreateProcessW(cmd.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                       CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &watched) == FALSE)
    {
        Fail("could not start the process to watch", static_cast<long long>(GetLastError()));
        return;
    }
    CloseHandle(watched.hThread);

    abyss::CursorController controller(GetModuleHandleW(nullptr));
    const HANDLE watchdog = StartApp(application, L"--watch " + std::to_wstring(watched.dwProcessId));
    if (watchdog == nullptr || !controller.Hide())
    {
        Fail("could not start the watchdog or hide the cursors");
        if (watchdog != nullptr)
        {
            TerminateProcess(watchdog, 1);
            CloseHandle(watchdog);
        }
        TerminateProcess(watched.hProcess, 1);
        CloseHandle(watched.hProcess);
        ReloadScheme();
        return;
    }

    if (WaitForExit(watchdog, 1500) != -1)
    {
        Fail("the watchdog exited while the watched process was still running");
    }
    if (RenderCursor(SystemCursor(kNormalRole)) != Rendered::invisible)
    {
        Fail("the cursors came back before the watched process ended");
    }

    TerminateProcess(watched.hProcess, 1);
    CloseHandle(watched.hProcess);
    const long long exit_code = WaitForExit(watchdog, 15000);
    CloseHandle(watchdog);
    const AllRoles after = RenderTargetRoles(true);
    std::printf("Watchdog: exited with %lld after the watched process was killed, cursors %s\n",
                exit_code, after == AllRoles::visible ? "all draw" : "NOT ALL DRAW");
    if (exit_code != 0 || after != AllRoles::visible)
    {
        Fail("the watchdog did not bring the cursors back", exit_code);
    }
}

// A cursor scheme can point at a file that no longer exists (a deleted cursor
// pack, a file on a disconnected drive). Restoring must not leave that role
// transparent.
void MissingFilePhase()
{
    constexpr wchar_t kCursorsKey[] = L"Control Panel\\Cursors";
    constexpr wchar_t kHandValue[] = L"Hand";

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kCursorsKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        Fail("could not open the cursor scheme key");
        return;
    }

    std::vector<BYTE> original(4096);
    DWORD original_type = REG_NONE;
    DWORD original_size = static_cast<DWORD>(original.size());
    const bool had_value = RegQueryValueExW(key, kHandValue, nullptr, &original_type,
                                            original.data(), &original_size) == ERROR_SUCCESS;

    constexpr wchar_t kMissing[] = L"C:\\AbyssCursorHideTest\\missing-hand.cur";
    (void)RegSetValueExW(key, kHandValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(kMissing),
                         static_cast<DWORD>(sizeof(kMissing)));
    ReloadScheme();
    const Rendered before = RenderCursor(SystemCursor(kHandRole));

    abyss::CursorController controller(GetModuleHandleW(nullptr));
    bool restored = false;
    if (controller.Hide())
    {
        restored = controller.Restore();
    }
    const Rendered after = RenderCursor(SystemCursor(kHandRole));
    std::printf("Missing file: hand %s after reloading, %s after hide and restore "
                "(Restore() reported %s)\n",
                before == Rendered::visible ? "draws" : "does not draw",
                after == Rendered::visible ? "draws" : "DOES NOT DRAW", restored ? "true" : "false");
    if (after != Rendered::visible)
    {
        Fail("a scheme entry pointing at a missing file stayed transparent after restoring");
    }

    if (had_value)
    {
        (void)RegSetValueExW(key, kHandValue, 0, original_type, original.data(), original_size);
    }
    else
    {
        (void)RegDeleteValueW(key, kHandValue);
    }
    RegCloseKey(key);
    ReloadScheme();
}

} // namespace

int wmain(const int argc, wchar_t** const argv)
{
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (!EnvironmentIs(L"ABYSS_SYSTEM_CURSOR_TESTS", L"1"))
    {
        std::printf("SKIP: these tests change this machine's system cursors; set "
                    "ABYSS_SYSTEM_CURSOR_TESTS=1 to run them (CI only).\n");
        return kSkip;
    }
    if (!EnvironmentIs(L"GITHUB_ACTIONS", L"true") &&
        !EnvironmentIs(L"ABYSS_SYSTEM_CURSOR_TESTS_LOCAL", L"1"))
    {
        std::printf("SKIP: not on GitHub Actions; set ABYSS_SYSTEM_CURSOR_TESTS_LOCAL=1 to run "
                    "them on a throwaway virtual machine.\n");
        return kSkip;
    }
    if (argc < 2)
    {
        std::printf("FAIL: pass the path to AbyssCursorHide.exe.\n");
        return 1;
    }
    const std::wstring application = argv[1];

    SetUnhandledExceptionFilter(ReloadOnCrash);
    PrintSession();

    const int width = GetSystemMetrics(SM_CXCURSOR);
    const int height = GetSystemMetrics(SM_CYCURSOR);
    const int probe = ProbeSession(width, height);
    if (probe != 0)
    {
        ReloadScheme();
        return probe;
    }

    ProbeRoles(width, height);
    StressHideRestore();
    QuotaPhase(application);
    WatchdogPhase(application);
    MissingFilePhase();

    ReloadScheme();
    if (g_failures == 0)
    {
        std::printf("All system cursor tests passed.\n");
        return 0;
    }
    std::printf("%d system cursor check(s) failed.\n", g_failures);
    return 1;
}
