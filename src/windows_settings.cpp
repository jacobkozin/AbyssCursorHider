#include "abyss/windows_settings.hpp"

#include <windows.h>

#include <cwchar>
#include <vector>

namespace abyss
{
namespace
{

constexpr wchar_t kSettingsKey[] = L"Software\\AbyssCursorHide";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kRunValueName[] = L"AbyssCursorHide";

bool ReadDword(const wchar_t* value_name, DWORD& value) noexcept
{
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value_name, RRF_RT_REG_DWORD, nullptr,
                        &value, &size) == ERROR_SUCCESS;
}

bool WriteDword(HKEY key, const wchar_t* value_name, const DWORD value) noexcept
{
    return RegSetValueExW(key, value_name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
}

// Task Manager's Startup apps switch leaves the Run value in place and records
// "off" in a separate StartupApproved value, so both have to be checked.
bool IsTurnedOffInTaskManager() noexcept
{
    BYTE data[64]{};
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kRunValueName, RRF_RT_REG_BINARY,
                     nullptr, data, &size) != ERROR_SUCCESS)
    {
        return false;
    }
    return IsStartupApprovalDisabled(data, size);
}

bool DeleteValue(const wchar_t* key, const wchar_t* value_name) noexcept
{
    const LSTATUS removed = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, value_name);
    return removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND ||
           removed == ERROR_PATH_NOT_FOUND;
}

} // namespace

AppSettings SettingsStore::Load() const noexcept
{
    AppSettings settings;
    DWORD value = 0;

    if (ReadDword(L"Enabled", value))
    {
        settings.enabled = value != 0;
    }
    if (ReadDword(L"DelaySeconds", value))
    {
        settings.delay_seconds = value;
    }

    return NormalizeSettings(settings);
}

bool SettingsStore::Save(const AppSettings& settings) const noexcept
{
    HKEY key = nullptr;
    const LSTATUS opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS)
    {
        return false;
    }

    const DWORD enabled = settings.enabled ? 1UL : 0UL;
    const DWORD delay = settings.delay_seconds;
    const bool saved =
        WriteDword(key, L"Enabled", enabled) && WriteDword(key, L"DelaySeconds", delay);
    RegCloseKey(key);
    return saved;
}

StartupRegistration::StartupRegistration(std::wstring executable_path)
    : executable_path_(std::move(executable_path)), command_(BuildStartupCommand(executable_path_))
{
}

bool StartupRegistration::IsEnabled() const noexcept
{
    if (command_.empty())
    {
        return false;
    }

    DWORD size = 0;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValueName, RRF_RT_REG_SZ, nullptr,
                                  nullptr, &size);
    if (status != ERROR_SUCCESS || size < sizeof(wchar_t))
    {
        return false;
    }

    std::vector<wchar_t> value((size / sizeof(wchar_t)) + 1, L'\0');
    status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValueName, RRF_RT_REG_SZ, nullptr,
                          value.data(), &size);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }

    const bool points_here =
        CompareStringOrdinal(value.data(), -1, command_.c_str(), -1, TRUE) == CSTR_EQUAL ||
        CompareStringOrdinal(value.data(), -1, executable_path_.c_str(), -1, TRUE) == CSTR_EQUAL;
    return points_here && !IsTurnedOffInTaskManager();
}

bool StartupRegistration::SetEnabled(const bool enabled) const noexcept
{
    if (enabled)
    {
        if (command_.empty())
        {
            return false;
        }

        HKEY key = nullptr;
        const LSTATUS opened =
            RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_SET_VALUE, nullptr, &key, nullptr);
        if (opened != ERROR_SUCCESS)
        {
            return false;
        }

        const DWORD bytes = static_cast<DWORD>((command_.size() + 1) * sizeof(wchar_t));
        const bool saved =
            RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(command_.c_str()), bytes) == ERROR_SUCCESS;
        RegCloseKey(key);

        // Turning it on from the tray menu should win over an older "off" from
        // Task Manager, otherwise Windows would keep skipping the entry.
        return saved && DeleteValue(kStartupApprovedKey, kRunValueName);
    }

    return DeleteValue(kRunKey, kRunValueName) && DeleteValue(kStartupApprovedKey, kRunValueName);
}

std::wstring CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::wstring CurrentExecutableDirectory()
{
    std::wstring path = CurrentExecutablePath();
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return {};
    }
    path.resize(separator);
    return path;
}

} // namespace abyss
