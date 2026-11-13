#include "abyss/core.hpp"
#include "abyss/cursor_controller.hpp"
#include "abyss/windows_settings.hpp"
#include "resource.h"

#include <shellapi.h>
#include <strsafe.h>
#include <windows.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr wchar_t kWindowClassName[] = L"AbyssCursorHide.Window.v5";
constexpr wchar_t kWindowTitle[] = L"Abyss Cursor Hide";
constexpr wchar_t kMutexName[] = L"Local\\AbyssCursorHide.Singleton";

constexpr UINT kTrayIconId = 1;
constexpr UINT_PTR kIdleTimerId = 1;
constexpr UINT kIdleTimerIntervalMs = 100;
constexpr UINT_PTR kTrayRetryTimerId = 2;
constexpr UINT kTrayRetryIntervalMs = 2000;
constexpr UINT kTrayRetryLimit = 60;
constexpr UINT kMessageTray = WM_APP + 1;
constexpr UINT kMessageShowMenu = WM_APP + 2;

constexpr UINT kCommandToggleEnabled = 1001;
constexpr UINT kCommandStartWithWindows = 1002;
constexpr UINT kCommandHideNow = 1003;
constexpr UINT kCommandExit = 1004;
constexpr UINT kCommandDelayBase = 1100;

constexpr std::array<const wchar_t*, abyss::kDelayOptionsSeconds.size()> kDelayLabels = {
    L"1 second",   L"2 seconds",  L"3 seconds",  L"5 seconds",
    L"10 seconds", L"15 seconds", L"30 seconds", L"60 seconds",
};

constexpr std::array<int, 5> kMouseButtons = {
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2,
};

// Absolute pointing devices (pens, tablets, remote desktop, virtual machines)
// report positions from 0 to 65535 across the screen. A change smaller than
// this, roughly a few pixels, is jitter or a repeat of the same position.
constexpr LONG kAbsoluteMoveThreshold = 64;

// Watchdog and restore-helper restarts wait this long, doubling up to the cap.
constexpr ULONGLONG kWatchdogFirstRetryMs = 5000;
constexpr ULONGLONG kWatchdogMaxRetryMs = 300000;
constexpr ULONGLONG kWatchdogHealthyRunMs = 60000;
constexpr ULONGLONG kRestoreFirstRetryMs = 1000;
constexpr ULONGLONG kRestoreMaxRetryMs = 60000;

LONG WINAPI RestoreAfterUnhandledException(EXCEPTION_POINTERS*) noexcept
{
    // Only the reload: a crashing process should do as little as possible.
    (void)abyss::ReloadSystemCursorScheme();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Returns the mandatory integrity level of a process (for example
// SECURITY_MANDATORY_MEDIUM_RID), or 0 if its token cannot be read.
DWORD ProcessIntegrityLevel(const HANDLE process) noexcept
{
    HANDLE token = nullptr;
    if (OpenProcessToken(process, TOKEN_QUERY, &token) == FALSE)
    {
        return 0;
    }

    alignas(TOKEN_MANDATORY_LABEL) BYTE buffer[sizeof(TOKEN_MANDATORY_LABEL) +
                                               SECURITY_MAX_SID_SIZE]{};
    DWORD size = 0;
    DWORD level = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &size) != FALSE)
    {
        const PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer)->Label.Sid;
        const UCHAR count = *GetSidSubAuthorityCount(sid);
        if (count > 0)
        {
            level = *GetSidSubAuthority(sid, count - 1U);
        }
    }
    CloseHandle(token);
    return level;
}

// "AbyssCursorHide.exe --watch <pid>" is the watchdog: a second copy of this
// program that waits for the tray app to exit, however it exits, and then
// reloads the cursor scheme. It has no window and does nothing else.
bool TryRunWatchdogMode(int& result) noexcept
{
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr)
    {
        return false;
    }

    const bool requested = argument_count == 3 && std::wstring_view(arguments[1]) == L"--watch";
    if (!requested)
    {
        LocalFree(arguments);
        return false;
    }

    const DWORD process_id = static_cast<DWORD>(abyss::ParseProcessId(arguments[2]));
    LocalFree(arguments);
    if (process_id == 0)
    {
        result = 2;
        return true;
    }

    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (process != nullptr)
    {
        (void)WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }

    result = abyss::ReloadSystemCursorScheme() ? 0 : 1;
    return true;
}

// "AbyssCursorHide.exe --restore <roles>" reloads the cursor scheme once,
// checks that the given cursor roles are visible again and exits with 0 if
// they are. The tray app starts it when its own reload leaves a cursor
// transparent: Windows loads the scheme inside the calling process, so a fresh
// process can succeed where a process that is short of handles cannot.
bool TryRunRestoreMode(int& result) noexcept
{
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr)
    {
        return false;
    }

    std::uint32_t roles = 0;
    const bool requested = argument_count == 3 &&
                           std::wstring_view(arguments[1]) == L"--restore" &&
                           abyss::ParseUnsignedDecimal(arguments[2], roles);
    LocalFree(arguments);
    if (!requested)
    {
        return false;
    }

    result = abyss::RestoreSystemCursorScheme(roles) ? 0 : 1;
    return true;
}

class Application final
{
  public:
    explicit Application(const HINSTANCE instance)
        : instance_(instance), cursor_(instance), settings_(settings_store_.Load()),
          executable_path_(abyss::CurrentExecutablePath()), startup_(executable_path_)
    {
    }

    ~Application()
    {
        Cleanup(false);
    }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run()
    {
        SetUnhandledExceptionFilter(RestoreAfterUnhandledException);

        // Every launch reloads the cursor scheme first, from this fresh
        // process, even when another copy is already running. So starting the
        // app again always brings back a pointer that a killed, hung or broken
        // copy left invisible.
        (void)abyss::ReloadSystemCursorScheme();

        if (!AcquireSingleInstance())
        {
            return 0;
        }

        if (!RegisterWindowClass() || !CreateMessageWindow())
        {
            ShowError(L"Abyss Cursor Hide could not create its background window.");
            Cleanup(false);
            return 1;
        }

        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        own_integrity_level_ = ProcessIntegrityLevel(GetCurrentProcess());
        if (own_integrity_level_ == 0)
        {
            own_integrity_level_ = SECURITY_MANDATORY_MEDIUM_RID;
        }
        InitializeFallbackInputState();
        session_notifications_registered_ =
            WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        ShowTrayIconOrRetry();

        timer_id_ = SetTimer(window_, kIdleTimerId, kIdleTimerIntervalMs, nullptr);
        if (timer_id_ == 0)
        {
            ShowError(L"Abyss Cursor Hide could not start its inactivity timer.");
            Cleanup(true);
            return 3;
        }

        raw_input_registered_ = RegisterRawMouseInput();
        last_activity_tick_ = GetTickCount64();
        StartWatchdog(last_activity_tick_);

        MSG message{};
        int result = 0;
        while (true)
        {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received == 0)
            {
                result = static_cast<int>(message.wParam);
                break;
            }
            if (received == -1)
            {
                result = 4;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        Cleanup(false);
        return result;
    }

  private:
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HANDLE mutex_ = nullptr;
    HICON tray_icon_ = nullptr;
    bool owns_tray_icon_ = false;
    bool tray_added_ = false;
    bool cleanup_started_ = false;
    bool menu_open_ = false;
    UINT_PTR timer_id_ = 0;
    UINT taskbar_created_message_ = 0;
    NOTIFYICONDATAW tray_data_{};
    UINT tray_retries_left_ = 0;
    bool raw_input_registered_ = false;
    bool session_notifications_registered_ = false;
    DWORD own_integrity_level_ = 0;

    abyss::CursorController cursor_;
    abyss::SettingsStore settings_store_;
    abyss::AppSettings settings_;
    std::wstring executable_path_;
    abyss::StartupRegistration startup_;

    ULONGLONG last_activity_tick_ = 0;
    POINT last_cursor_position_{};
    bool have_cursor_position_ = false;
    std::array<bool, kMouseButtons.size()> button_down_{};
    LONG last_absolute_x_ = 0;
    LONG last_absolute_y_ = 0;
    bool have_absolute_position_ = false;

    // The watchdog process, restarted with a growing delay if it ever exits.
    HANDLE watchdog_process_ = nullptr;
    ULONGLONG watchdog_started_tick_ = 0;
    ULONGLONG next_watchdog_start_tick_ = 0;
    ULONGLONG watchdog_retry_ms_ = 0;

    // Set when a restore could not be confirmed; until a --restore helper
    // confirms it, the helper is retried and the pointer is not hidden again.
    bool restore_confirmed_ = true;
    HANDLE restore_helper_ = nullptr;
    ULONGLONG next_restore_attempt_tick_ = 0;
    ULONGLONG restore_retry_ms_ = 0;

    bool AcquireSingleInstance()
    {
        mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
        if (mutex_ == nullptr)
        {
            ShowError(L"Abyss Cursor Hide could not create its single-instance guard.");
            return false;
        }

        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return true;
        }

        const HWND existing = FindWindowW(kWindowClassName, kWindowTitle);
        if (existing != nullptr)
        {
            // This copy was just started by the user, so it may pass on the
            // right to take the foreground. Without that, the running copy's
            // menu would not close when the user clicks somewhere else.
            DWORD existing_process_id = 0;
            GetWindowThreadProcessId(existing, &existing_process_id);
            AllowSetForegroundWindow(existing_process_id);
            PostMessageW(existing, kMessageShowMenu, 0, 0);
        }
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }

    bool RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = WindowProcedure;
        window_class.hInstance = instance_;
        window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClassName;
        window_class.hIconSm = window_class.hIcon;
        return RegisterClassExW(&window_class) != 0;
    }

    bool CreateMessageWindow()
    {
        window_ =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClassName, kWindowTitle,
                            WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
        return window_ != nullptr;
    }

    // Loads the tray icon at the small-icon size for the taskbar's DPI, which
    // can change while the app runs (docking, a new monitor, display settings).
    void LoadTrayIcon() noexcept
    {
        const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        UINT dpi = taskbar != nullptr ? GetDpiForWindow(taskbar) : 0;
        if (dpi == 0)
        {
            dpi = GetDpiForSystem();
        }

        const auto icon = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi),
            LR_DEFAULTCOLOR));
        if (icon == nullptr && tray_icon_ != nullptr)
        {
            return;
        }

        if (owns_tray_icon_ && tray_icon_ != nullptr)
        {
            DestroyIcon(tray_icon_);
        }
        tray_icon_ = icon != nullptr ? icon : LoadIconW(nullptr, IDI_APPLICATION);
        owns_tray_icon_ = icon != nullptr;
    }

    // Shows the notification-area icon. NIM_ADD fails when Explorer already
    // has it, which happens when Windows sends TaskbarCreated after a DPI or
    // monitor change rather than an Explorer restart, or when an add that
    // timed out at sign-in went through after all; NIM_MODIFY covers both.
    bool ShowTrayIcon() noexcept
    {
        LoadTrayIcon();
        tray_data_ = {};
        tray_data_.cbSize = sizeof(tray_data_);
        tray_data_.hWnd = window_;
        tray_data_.uID = kTrayIconId;
        tray_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray_data_.uCallbackMessage = kMessageTray;
        tray_data_.hIcon = tray_icon_;
        FormatTooltip();

        tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_data_) != FALSE ||
                      Shell_NotifyIconW(NIM_MODIFY, &tray_data_) != FALSE;
        if (tray_added_)
        {
            tray_data_.uVersion = NOTIFYICON_VERSION_4;
            (void)Shell_NotifyIconW(NIM_SETVERSION, &tray_data_);
        }
        return tray_added_;
    }

    // Explorer can be slow to take the icon at sign-in, so keep trying for a
    // while instead of giving up. The app works without the icon meanwhile.
    void ShowTrayIconOrRetry() noexcept
    {
        if (ShowTrayIcon())
        {
            KillTimer(window_, kTrayRetryTimerId);
            return;
        }
        tray_retries_left_ = kTrayRetryLimit;
        (void)SetTimer(window_, kTrayRetryTimerId, kTrayRetryIntervalMs, nullptr);
    }

    void OnTrayRetryTimer() noexcept
    {
        if (ShowTrayIcon() || tray_retries_left_ == 0 || --tray_retries_left_ == 0)
        {
            KillTimer(window_, kTrayRetryTimerId);
        }
    }

    void RemoveTrayIcon() noexcept
    {
        if (window_ != nullptr && tray_data_.cbSize != 0)
        {
            (void)Shell_NotifyIconW(NIM_DELETE, &tray_data_);
        }
        tray_added_ = false;
    }

    void FormatTooltip() noexcept
    {
        if (settings_.enabled)
        {
            StringCchPrintfW(tray_data_.szTip, ARRAYSIZE(tray_data_.szTip),
                             L"Abyss Cursor Hide: hides after %u s", settings_.delay_seconds);
        }
        else
        {
            StringCchCopyW(tray_data_.szTip, ARRAYSIZE(tray_data_.szTip),
                           L"Abyss Cursor Hide: paused");
        }
    }

    void UpdateTrayIcon() noexcept
    {
        if (!tray_added_)
        {
            return;
        }
        FormatTooltip();
        tray_data_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray_data_.hIcon = tray_icon_;
        Shell_NotifyIconW(NIM_MODIFY, &tray_data_);
    }

    void InitializeFallbackInputState() noexcept
    {
        have_cursor_position_ = GetCursorPos(&last_cursor_position_) != FALSE;
        for (std::size_t index = 0; index < kMouseButtons.size(); ++index)
        {
            button_down_[index] = (GetAsyncKeyState(kMouseButtons[index]) & 0x8000) != 0;
        }
    }

    bool RegisterRawMouseInput() const noexcept
    {
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = window_;
        return RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    }

    void UnregisterRawMouseInput() noexcept
    {
        if (!raw_input_registered_)
        {
            return;
        }
        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_REMOVE;
        device.hwndTarget = nullptr;
        (void)RegisterRawInputDevices(&device, 1, sizeof(device));
        raw_input_registered_ = false;
    }

    void OnRawMouseInput(const HRAWINPUT handle) noexcept
    {
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied =
            GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1) || copied < sizeof(RAWINPUTHEADER) ||
            input.header.dwType != RIM_TYPEMOUSE)
        {
            return;
        }

        constexpr USHORT kButtonActivity =
            RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN |
            RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL;
        const RAWMOUSE& mouse = input.data.mouse;

        bool moved = false;
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
        {
            // Pens, tablets, remote desktop and virtual machines report where
            // the pointer is, and some keep repeating the same position, which
            // would keep the pointer from ever hiding. Count real changes only.
            moved = !have_absolute_position_ ||
                    std::abs(mouse.lLastX - last_absolute_x_) > kAbsoluteMoveThreshold ||
                    std::abs(mouse.lLastY - last_absolute_y_) > kAbsoluteMoveThreshold;
            last_absolute_x_ = mouse.lLastX;
            last_absolute_y_ = mouse.lLastY;
            have_absolute_position_ = true;
        }
        else
        {
            moved = mouse.lLastX != 0 || mouse.lLastY != 0;
        }

        if (moved || (mouse.usButtonFlags & kButtonActivity) != 0)
        {
            OnMouseActivity();
        }
    }

    bool PollMouseState() noexcept
    {
        bool activity = false;
        POINT position{};
        if (GetCursorPos(&position) == FALSE)
        {
            // GetCursorPos requires this thread's desktop to be the active input
            // desktop. A failure is expected during a secure-desktop transition
            // such as UAC or workstation lock. Never leave system cursors hidden
            // while input is isolated from this process.
            have_cursor_position_ = false;
            button_down_.fill(false);
            last_activity_tick_ = GetTickCount64();
            RestorePointer();
            return false;
        }

        if (!have_cursor_position_ || position.x != last_cursor_position_.x ||
            position.y != last_cursor_position_.y)
        {
            activity = have_cursor_position_;
            last_cursor_position_ = position;
            have_cursor_position_ = true;
        }

        // Only the current state is read. The "pressed since the last call" bit
        // is shared by every program in the session, so reading it would take
        // it away from them, and Raw Input already sees quick clicks.
        for (std::size_t index = 0; index < kMouseButtons.size(); ++index)
        {
            const bool down = (GetAsyncKeyState(kMouseButtons[index]) & 0x8000) != 0;
            if (down && !button_down_[index])
            {
                activity = true;
            }
            button_down_[index] = down;
        }

        if (activity)
        {
            OnMouseActivity();
        }
        return true;
    }

    void OnTimer()
    {
        // Raw Input normally restores immediately. Polling as a redundant path
        // also covers elevated foreground windows such as Task Manager, where a
        // normal-privilege background process cannot rely on every input message.
        if (!PollMouseState())
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        SuperviseWatchdog(now);
        SuperviseRestore(now);

        // Paused only stops automatic hiding. A pointer hidden with Hide Now
        // stays hidden until the mouse is used, like any other hide.
        if (!settings_.enabled)
        {
            return;
        }

        const ULONGLONG delay_ms = static_cast<ULONGLONG>(settings_.delay_seconds) * 1000ULL;
        if (cursor_.hidden() || now - last_activity_tick_ < delay_ms)
        {
            return;
        }

        // Wait for another full delay instead of hiding while the tray menu is
        // open, while a button is held (a paused drag or a held scroll arrow),
        // or over a more privileged window, where Windows hides clicks and the
        // wheel from this app so they could not bring the pointer back.
        if (menu_open_ || AnyButtonDown() || ForegroundIsMorePrivileged() || !HidePointer())
        {
            last_activity_tick_ = now;
        }
    }

    bool AnyButtonDown() const noexcept
    {
        return std::any_of(button_down_.begin(), button_down_.end(),
                           [](const bool down) { return down; });
    }

    // True when the foreground window belongs to a program with a higher
    // integrity level than this app (Task Manager, an admin terminal), or to
    // one whose level cannot even be read.
    bool ForegroundIsMorePrivileged() const noexcept
    {
        const HWND foreground = GetForegroundWindow();
        DWORD process_id = 0;
        if (foreground == nullptr || GetWindowThreadProcessId(foreground, &process_id) == 0 ||
            process_id == GetCurrentProcessId())
        {
            return false;
        }

        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
        if (process == nullptr)
        {
            return true;
        }
        const DWORD level = ProcessIntegrityLevel(process);
        CloseHandle(process);
        return level == 0 || level > own_integrity_level_;
    }

    void OnMouseActivity() noexcept
    {
        last_activity_tick_ = GetTickCount64();
        RestorePointer();
    }

    // Brings the pointer back. If the reload in this process leaves a cursor
    // transparent, a fresh copy of the app reloads the scheme instead (see
    // TryRunRestoreMode and SuperviseRestore).
    void RestorePointer() noexcept
    {
        if (!cursor_.Restore())
        {
            RequireConfirmedRestore();
        }
    }

    // Hides only while the safety net is in place: the watchdog is running and
    // the last restore is confirmed. A failed hide has already reloaded the
    // scheme; if a cursor is still transparent after that, a fresh copy of the
    // app reloads it.
    bool HidePointer() noexcept
    {
        if (!WatchdogRunning() || !restore_confirmed_)
        {
            return false;
        }
        if (cursor_.Hide())
        {
            return true;
        }
        if (!abyss::CursorRolesVisible(cursor_.expected_roles()))
        {
            RequireConfirmedRestore();
        }
        return false;
    }

    // The next timer tick starts the helper, so starting a process never
    // stalls the handling of mouse input.
    void RequireConfirmedRestore() noexcept
    {
        restore_confirmed_ = false;
        restore_retry_ms_ = 0;
        next_restore_attempt_tick_ = 0;
    }

    // Runs one --restore helper at a time until one exits with 0, waiting
    // longer after each failure.
    void SuperviseRestore(const ULONGLONG now) noexcept
    {
        if (restore_confirmed_)
        {
            return;
        }

        if (restore_helper_ != nullptr)
        {
            if (WaitForSingleObject(restore_helper_, 0) == WAIT_TIMEOUT)
            {
                return;
            }
            DWORD exit_code = 1;
            const bool succeeded =
                GetExitCodeProcess(restore_helper_, &exit_code) != FALSE && exit_code == 0;
            CloseHandle(restore_helper_);
            restore_helper_ = nullptr;
            if (succeeded)
            {
                restore_confirmed_ = true;
                return;
            }
        }

        if (now < next_restore_attempt_tick_)
        {
            return;
        }
        restore_helper_ =
            StartCopy(abyss::BuildRestoreCommand(executable_path_, cursor_.expected_roles()));
        restore_retry_ms_ = restore_retry_ms_ == 0
                                ? kRestoreFirstRetryMs
                                : std::min(restore_retry_ms_ * 2, kRestoreMaxRetryMs);
        next_restore_attempt_tick_ = now + restore_retry_ms_;
    }

    bool WatchdogRunning() const noexcept
    {
        return watchdog_process_ != nullptr &&
               WaitForSingleObject(watchdog_process_, 0) == WAIT_TIMEOUT;
    }

    // Restarts the watchdog if it exits while the app is still running, for
    // example if somebody ends it in Task Manager. A watchdog that keeps dying
    // soon after it starts is restarted less and less often.
    void SuperviseWatchdog(const ULONGLONG now) noexcept
    {
        if (WatchdogRunning())
        {
            return;
        }

        if (watchdog_process_ != nullptr)
        {
            CloseHandle(watchdog_process_);
            watchdog_process_ = nullptr;
            const bool short_lived = now - watchdog_started_tick_ < kWatchdogHealthyRunMs;
            watchdog_retry_ms_ = short_lived ? std::min(std::max(watchdog_retry_ms_ * 2,
                                                                 kWatchdogFirstRetryMs),
                                                        kWatchdogMaxRetryMs)
                                             : kWatchdogFirstRetryMs;
            next_watchdog_start_tick_ = now + watchdog_retry_ms_;
        }

        if (now >= next_watchdog_start_tick_)
        {
            StartWatchdog(now);
        }
    }

    HMENU BuildTrayMenu() const
    {
        HMENU menu = CreatePopupMenu();
        HMENU delay_menu = CreatePopupMenu();
        if (menu == nullptr || delay_menu == nullptr)
        {
            if (menu != nullptr)
            {
                DestroyMenu(menu);
            }
            if (delay_menu != nullptr)
            {
                DestroyMenu(delay_menu);
            }
            return nullptr;
        }

        AppendMenuW(menu, MF_STRING | (settings_.enabled ? MF_CHECKED : MF_UNCHECKED),
                    kCommandToggleEnabled, L"&Auto-hide enabled");

        for (std::size_t index = 0; index < abyss::kDelayOptionsSeconds.size(); ++index)
        {
            const UINT flags =
                MF_STRING |
                (settings_.delay_seconds == abyss::kDelayOptionsSeconds[index] ? MF_CHECKED
                                                                               : MF_UNCHECKED);
            AppendMenuW(delay_menu, flags, kCommandDelayBase + static_cast<UINT>(index),
                        kDelayLabels[index]);
        }

        // Destroying the menu also destroys an attached submenu, but not one
        // that failed to attach.
        if (AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(delay_menu), L"Hide &delay") ==
            FALSE)
        {
            DestroyMenu(delay_menu);
        }
        AppendMenuW(menu, MF_STRING | (startup_.IsEnabled() ? MF_CHECKED : MF_UNCHECKED),
                    kCommandStartWithWindows, L"&Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandHideNow, L"&Hide Now");
        AppendMenuW(menu, MF_STRING, kCommandExit, L"E&xit");
        return menu;
    }

    // Where a menu opened by a second launch goes: at the tray icon if
    // Explorer can say where it is, otherwise at the pointer.
    bool TrayIconAnchor(POINT& anchor) const noexcept
    {
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = window_;
        identifier.uID = kTrayIconId;
        RECT icon{};
        if (tray_added_ && SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &icon)))
        {
            anchor = {(icon.left + icon.right) / 2, icon.top};
            return true;
        }
        return GetCursorPos(&anchor) != FALSE;
    }

    // Opens the tray menu above `anchor`, the point the shell reported for the
    // click or key press, so a menu opened from the keyboard appears at the
    // icon rather than at a pointer that may be hidden.
    void ShowTrayMenu(POINT anchor, const bool opened_from_keyboard)
    {
        if (menu_open_ || cleanup_started_)
        {
            return;
        }

        const abyss::ScreenPoint raised_anchor = abyss::RaiseMenuAnchor({anchor.x, anchor.y});
        anchor.x = raised_anchor.x;
        anchor.y = raised_anchor.y;

        menu_open_ = true;
        bool reopen = true;
        while (reopen && !cleanup_started_)
        {
            HMENU menu = BuildTrayMenu();
            if (menu == nullptr)
            {
                break;
            }

            SetForegroundWindow(window_);
            const UINT command = TrackPopupMenuEx(menu,
                                                  TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY |
                                                      TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                                                  anchor.x, anchor.y, window_, nullptr);
            DestroyMenu(menu);

            // Escape from a menu opened with the keyboard hands focus back to
            // the notification area, as the shell asks.
            if (command == 0 && opened_from_keyboard && tray_added_)
            {
                (void)Shell_NotifyIconW(NIM_SETFOCUS, &tray_data_);
            }

            reopen = command != 0 && ExecuteCommand(command);
            PostMessageW(window_, WM_NULL, 0, 0);
        }
        menu_open_ = false;
    }

    bool ExecuteCommand(const UINT command)
    {
        if (command == kCommandToggleEnabled)
        {
            abyss::AppSettings candidate = settings_;
            candidate.enabled = !candidate.enabled;
            if (SaveSettings(candidate))
            {
                ApplySettingsChange();
            }
            return true;
        }

        if (command == kCommandStartWithWindows)
        {
            const bool enable = !startup_.IsEnabled();
            if (!startup_.SetEnabled(enable))
            {
                ShowError(L"The Start with Windows setting could not be updated.");
            }
            return true;
        }

        if (command == kCommandHideNow)
        {
            // Take a fresh polling baseline first, so the pointer's trip to
            // this menu item does not count as new movement on the next tick
            // and bring it straight back.
            InitializeFallbackInputState();
            last_activity_tick_ = GetTickCount64();
            if (!HidePointer())
            {
                ShowError(L"The pointer can't be hidden safely right now. Abyss Cursor Hide "
                          L"only hides it while its watchdog is running and the last restore "
                          L"worked. Try again in a few seconds.");
            }
            return false;
        }

        if (command == kCommandExit)
        {
            Cleanup(true);
            return false;
        }

        if (command >= kCommandDelayBase &&
            command < kCommandDelayBase + abyss::kDelayOptionsSeconds.size())
        {
            const std::size_t index = command - kCommandDelayBase;
            abyss::AppSettings candidate = settings_;
            candidate.delay_seconds = abyss::kDelayOptionsSeconds[index];
            if (SaveSettings(candidate))
            {
                ApplySettingsChange();
            }
            return true;
        }

        return false;
    }

    bool SaveSettings(const abyss::AppSettings& candidate)
    {
        if (!settings_store_.Save(candidate))
        {
            ShowError(L"The settings could not be saved to your Windows user profile.");
            return false;
        }
        settings_ = candidate;
        return true;
    }

    void ApplySettingsChange() noexcept
    {
        last_activity_tick_ = GetTickCount64();
        RestorePointer();
        UpdateTrayIcon();
    }

    // In-process cleanup cannot run when the app is killed (taskkill /F, a
    // crash inside a system DLL), so a second copy of this executable watches
    // it from outside. See TryRunWatchdogMode.
    void StartWatchdog(const ULONGLONG now) noexcept
    {
        watchdog_process_ =
            StartCopy(abyss::BuildWatchdogCommand(executable_path_, GetCurrentProcessId()));
        watchdog_started_tick_ = now;
        if (watchdog_process_ == nullptr)
        {
            watchdog_retry_ms_ =
                std::min(std::max(watchdog_retry_ms_ * 2, kWatchdogFirstRetryMs), kWatchdogMaxRetryMs);
            next_watchdog_start_tick_ = now + watchdog_retry_ms_;
        }
    }

    // Starts another copy of this executable with the given command line and
    // returns its process handle, or nullptr if it could not be started.
    HANDLE StartCopy(const std::wstring& command) const noexcept
    {
        try
        {
            const std::wstring directory = abyss::CurrentExecutableDirectory();
            if (directory.empty() || command.empty())
            {
                return nullptr;
            }

            std::vector<wchar_t> mutable_command(command.begin(), command.end());
            mutable_command.push_back(L'\0');

            STARTUPINFOW startup_info{};
            startup_info.cb = sizeof(startup_info);
            PROCESS_INFORMATION process_info{};
            if (CreateProcessW(executable_path_.c_str(), mutable_command.data(), nullptr, nullptr,
                               FALSE, 0, nullptr, directory.c_str(), &startup_info,
                               &process_info) == FALSE)
            {
                return nullptr;
            }
            CloseHandle(process_info.hThread);
            return process_info.hProcess;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void Cleanup(const bool destroy_window) noexcept
    {
        if (cleanup_started_)
        {
            return;
        }
        cleanup_started_ = true;

        (void)cursor_.Restore();
        UnregisterRawMouseInput();
        if (session_notifications_registered_ && window_ != nullptr)
        {
            (void)WTSUnRegisterSessionNotification(window_);
            session_notifications_registered_ = false;
        }
        if (timer_id_ != 0 && window_ != nullptr)
        {
            KillTimer(window_, timer_id_);
            timer_id_ = 0;
        }
        if (window_ != nullptr)
        {
            KillTimer(window_, kTrayRetryTimerId);
        }
        RemoveTrayIcon();

        if (destroy_window && window_ != nullptr && IsWindow(window_) != FALSE)
        {
            DestroyWindow(window_);
        }
        window_ = nullptr;

        if (owns_tray_icon_ && tray_icon_ != nullptr)
        {
            DestroyIcon(tray_icon_);
        }
        tray_icon_ = nullptr;
        owns_tray_icon_ = false;

        if (mutex_ != nullptr)
        {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }

        // Only close the handles: the watchdog has to outlive this process so
        // it can reload the scheme after it exits.
        if (watchdog_process_ != nullptr)
        {
            CloseHandle(watchdog_process_);
            watchdog_process_ = nullptr;
        }
        if (restore_helper_ != nullptr)
        {
            CloseHandle(restore_helper_);
            restore_helper_ = nullptr;
        }
    }

    void ShowError(const wchar_t* message) const noexcept
    {
        MessageBoxW(window_, message, kWindowTitle, MB_OK | MB_ICONERROR);
    }

    LRESULT HandleMessage(const HWND window, const UINT message, const WPARAM w_param,
                          const LPARAM l_param)
    {
        if (message == kMessageTray)
        {
            // With NOTIFYICON_VERSION_4 the shell sends one of these per click
            // or key press, with the anchor point in wParam: NIN_SELECT for a
            // left click, WM_CONTEXTMENU for a right click or the menu key, and
            // NIN_KEYSELECT for Enter or Space on the focused icon.
            const UINT event = LOWORD(static_cast<DWORD_PTR>(l_param));
            if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_CONTEXTMENU)
            {
                ShowTrayMenu(POINT{GET_X_LPARAM(w_param), GET_Y_LPARAM(w_param)},
                             event == NIN_KEYSELECT);
            }
            return 0;
        }

        if (message == kMessageShowMenu)
        {
            POINT anchor{};
            if (TrayIconAnchor(anchor))
            {
                ShowTrayMenu(anchor, false);
            }
            return 0;
        }

        if (taskbar_created_message_ != 0 && message == taskbar_created_message_)
        {
            // Explorer restarted, or the taskbar's DPI changed. Bring a hidden
            // pointer back first: the shell calls below can block for seconds
            // while Explorer is busy, and input waits for them.
            OnMouseActivity();
            ShowTrayIconOrRetry();
            return 0;
        }

        switch (message)
        {
        case WM_INPUT:
            OnRawMouseInput(reinterpret_cast<HRAWINPUT>(l_param));
            return DefWindowProcW(window, message, w_param, l_param);

        case WM_TIMER:
            if (w_param == kIdleTimerId)
            {
                OnTimer();
            }
            else if (w_param == kTrayRetryTimerId)
            {
                OnTrayRetryTimer();
            }
            return 0;

        case WM_POWERBROADCAST:
            // Going to sleep and waking up. Other power broadcasts, such as a
            // laptop's battery level changing, are not mouse activity.
            if (w_param == PBT_APMSUSPEND || w_param == PBT_APMRESUMESUSPEND ||
                w_param == PBT_APMRESUMEAUTOMATIC)
            {
                OnMouseActivity();
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            // Restore for lock, unlock, console/RDP connect and disconnect, and
            // any other transition affecting this session. Polling remains the
            // fallback when Terminal Services notifications are unavailable.
            OnMouseActivity();
            InitializeFallbackInputState();
            return 0;

        case WM_QUERYENDSESSION:
            (void)cursor_.Restore();
            return TRUE;

        case WM_ENDSESSION:
            // Also sent when Restart Manager closes the app for an update
            // while the session goes on, so exit instead of hiding again.
            if (w_param != FALSE)
            {
                Cleanup(true);
            }
            return 0;

        case WM_CLOSE:
            Cleanup(true);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, w_param, l_param);
        }
    }

    static LRESULT CALLBACK WindowProcedure(const HWND window, const UINT message,
                                            const WPARAM w_param, const LPARAM l_param)
    {
        Application* application =
            reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            // Only the window's own creation may set the pointer. Any later
            // WM_NCCREATE was sent by another program.
            if (application != nullptr)
            {
                return TRUE;
            }
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            application = static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        }

        if (application != nullptr)
        {
            return application->HandleMessage(window, message, w_param, l_param);
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }
};

} // namespace

int WINAPI wWinMain(_In_ const HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    // Load DLLs only from System32, never from the app's own folder, so a DLL
    // dropped next to the exe cannot run inside it. wtsapi32.dll, the one
    // import that is not a KnownDLL, is delay-loaded so this applies to it too.
    (void)SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    int mode_result = 0;
    if (TryRunWatchdogMode(mode_result) || TryRunRestoreMode(mode_result))
    {
        return mode_result;
    }

    Application application(instance);
    return application.Run();
}
