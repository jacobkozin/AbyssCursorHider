#include "abyss/core.hpp"
#include "abyss/cursor_controller.hpp"
#include "abyss/windows_settings.hpp"
#include "resource.h"

#include <shellapi.h>
#include <strsafe.h>
#include <windows.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <array>
#include <cstdint>
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

LONG WINAPI RestoreAfterUnhandledException(EXCEPTION_POINTERS*) noexcept
{
    (void)abyss::RestoreSystemCursorScheme();
    return EXCEPTION_CONTINUE_SEARCH;
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

    result = abyss::RestoreSystemCursorScheme() ? 0 : 1;
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

        if (!AcquireSingleInstance())
        {
            return 0;
        }

        // If an earlier copy was killed together with its watchdog while the
        // pointer was hidden, starting the app again brings the pointer back.
        (void)abyss::RestoreSystemCursorScheme();

        if (!RegisterWindowClass() || !CreateMessageWindow())
        {
            ShowError(L"Abyss Cursor Hide could not create its background window.");
            Cleanup(false);
            return 1;
        }

        LoadApplicationIcon();
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        InitializeFallbackInputState();
        session_notifications_registered_ =
            WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) != FALSE;

        if (!AddTrayIcon())
        {
            ShowError(L"Abyss Cursor Hide could not create its notification-area icon.");
            Cleanup(true);
            return 2;
        }

        timer_id_ = SetTimer(window_, kIdleTimerId, kIdleTimerIntervalMs, nullptr);
        if (timer_id_ == 0)
        {
            ShowError(L"Abyss Cursor Hide could not start its inactivity timer.");
            Cleanup(true);
            return 3;
        }

        raw_input_registered_ = RegisterRawMouseInput();
        last_activity_tick_ = GetTickCount64();
        LaunchWatchdog();

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
    bool raw_input_registered_ = false;
    bool session_notifications_registered_ = false;

    abyss::CursorController cursor_;
    abyss::SettingsStore settings_store_;
    abyss::AppSettings settings_;
    std::wstring executable_path_;
    abyss::StartupRegistration startup_;

    ULONGLONG last_activity_tick_ = 0;
    POINT last_cursor_position_{};
    bool have_cursor_position_ = false;
    std::array<bool, kMouseButtons.size()> button_down_{};

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

    void LoadApplicationIcon()
    {
        const int width = GetSystemMetrics(SM_CXSMICON);
        const int height = GetSystemMetrics(SM_CYSMICON);
        tray_icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON),
                                                   IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
        if (tray_icon_ != nullptr)
        {
            owns_tray_icon_ = true;
            return;
        }

        tray_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
        owns_tray_icon_ = false;
    }

    bool AddTrayIcon()
    {
        tray_data_ = {};
        tray_data_.cbSize = sizeof(tray_data_);
        tray_data_.hWnd = window_;
        tray_data_.uID = kTrayIconId;
        tray_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray_data_.uCallbackMessage = kMessageTray;
        tray_data_.hIcon = tray_icon_;
        FormatTooltip();

        if (Shell_NotifyIconW(NIM_ADD, &tray_data_) == FALSE)
        {
            return false;
        }

        tray_added_ = true;
        tray_data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray_data_);
        return true;
    }

    void RemoveTrayIcon() noexcept
    {
        if (tray_added_)
        {
            Shell_NotifyIconW(NIM_DELETE, &tray_data_);
            tray_added_ = false;
        }
    }

    void FormatTooltip() noexcept
    {
        const wchar_t* state = settings_.enabled ? L"Enabled" : L"Paused";
        StringCchPrintfW(tray_data_.szTip, ARRAYSIZE(tray_data_.szTip),
                         L"Abyss Cursor Hide - %s - %u s", state, settings_.delay_seconds);
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
        const bool moved =
            mouse.lLastX != 0 || mouse.lLastY != 0 || (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
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
            (void)cursor_.Restore();
            return false;
        }

        if (!have_cursor_position_ || position.x != last_cursor_position_.x ||
            position.y != last_cursor_position_.y)
        {
            activity = have_cursor_position_;
            last_cursor_position_ = position;
            have_cursor_position_ = true;
        }

        for (std::size_t index = 0; index < kMouseButtons.size(); ++index)
        {
            const SHORT state = GetAsyncKeyState(kMouseButtons[index]);
            const bool down = (state & 0x8000) != 0;
            if ((down && !button_down_[index]) || (state & 0x0001) != 0)
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

        if (!settings_.enabled)
        {
            (void)cursor_.Restore();
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG delay_ms = static_cast<ULONGLONG>(settings_.delay_seconds) * 1000ULL;
        if (!cursor_.hidden() && now - last_activity_tick_ >= delay_ms)
        {
            if (!cursor_.Hide())
            {
                last_activity_tick_ = now;
            }
        }
    }

    void OnMouseActivity() noexcept
    {
        last_activity_tick_ = GetTickCount64();
        (void)cursor_.Restore();
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
                    kCommandToggleEnabled, L"Auto-hide enabled");

        for (std::size_t index = 0; index < abyss::kDelayOptionsSeconds.size(); ++index)
        {
            const UINT flags =
                MF_STRING |
                (settings_.delay_seconds == abyss::kDelayOptionsSeconds[index] ? MF_CHECKED
                                                                               : MF_UNCHECKED);
            AppendMenuW(delay_menu, flags, kCommandDelayBase + static_cast<UINT>(index),
                        kDelayLabels[index]);
        }

        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(delay_menu), L"Hide delay");
        AppendMenuW(menu, MF_STRING | (startup_.IsEnabled() ? MF_CHECKED : MF_UNCHECKED),
                    kCommandStartWithWindows, L"Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandHideNow, L"Hide Now");
        AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
        return menu;
    }

    void ShowTrayMenu()
    {
        if (menu_open_ || cleanup_started_)
        {
            return;
        }

        POINT anchor{};
        if (GetCursorPos(&anchor) == FALSE)
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
            last_activity_tick_ = GetTickCount64();
            if (!cursor_.Hide())
            {
                ShowError(L"The Windows cursor scheme could not be hidden safely.");
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
        (void)cursor_.Restore();
        UpdateTrayIcon();
    }

    // In-process cleanup cannot run when the app is killed (Task Manager,
    // taskkill /F, a crash inside a system DLL), so a second copy of this
    // executable watches it from outside. See TryRunWatchdogMode.
    void LaunchWatchdog() const
    {
        const std::wstring directory = abyss::CurrentExecutableDirectory();
        const std::wstring command =
            abyss::BuildWatchdogCommand(executable_path_, GetCurrentProcessId());
        if (directory.empty() || command.empty())
        {
            return;
        }

        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        if (CreateProcessW(executable_path_.c_str(), mutable_command.data(), nullptr, nullptr,
                           FALSE, 0, nullptr, directory.c_str(), &startup_info,
                           &process_info) != FALSE)
        {
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
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
            const UINT event = LOWORD(static_cast<DWORD_PTR>(l_param));
            if (event == WM_CONTEXTMENU || event == WM_LBUTTONUP || event == WM_RBUTTONUP ||
                event == WM_LBUTTONDBLCLK || event == NIN_SELECT || event == NIN_KEYSELECT)
            {
                ShowTrayMenu();
            }
            return 0;
        }

        if (message == kMessageShowMenu)
        {
            ShowTrayMenu();
            return 0;
        }

        if (taskbar_created_message_ != 0 && message == taskbar_created_message_)
        {
            tray_added_ = false;
            (void)AddTrayIcon();
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
            return 0;

        case WM_POWERBROADCAST:
            OnMouseActivity();
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
            if (w_param != FALSE)
            {
                (void)cursor_.Restore();
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
    int watchdog_result = 0;
    if (TryRunWatchdogMode(watchdog_result))
    {
        return watchdog_result;
    }

    Application application(instance);
    return application.Run();
}
