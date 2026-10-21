#include "abyss/core.hpp"

#include <iostream>
#include <string_view>

namespace
{

int g_failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

} // namespace

int main()
{
    using abyss::AppSettings;

    for (const std::uint32_t delay : abyss::kDelayOptionsSeconds)
    {
        Check(abyss::IsAllowedDelay(delay), "documented delay should be accepted");
    }
    Check(!abyss::IsAllowedDelay(0), "zero delay should be rejected");
    Check(!abyss::IsAllowedDelay(4), "undocumented delay should be rejected");
    Check(!abyss::IsAllowedDelay(3600), "oversized delay should be rejected");

    const AppSettings normalized = abyss::NormalizeSettings({false, 999});
    Check(!normalized.enabled, "normalization should preserve the enabled state");
    Check(normalized.delay_seconds == abyss::kDefaultDelaySeconds,
          "invalid delay should fall back to the default");

    Check(abyss::BuildStartupCommand(L"C:\\Program Files\\Abyss\\AbyssCursorHide.exe") ==
              L"\"C:\\Program Files\\Abyss\\AbyssCursorHide.exe\"",
          "startup command should quote the executable path");
    Check(abyss::BuildStartupCommand(L"").empty(), "empty executable path should be rejected");
    Check(abyss::BuildStartupCommand(L"C:\\bad\"path.exe").empty(),
          "embedded quote should be rejected");

    Check(abyss::ParseProcessId(L"12345") == 12345U, "valid process ID should be accepted");
    Check(abyss::ParseProcessId(L"") == 0U, "empty process ID should be rejected");
    Check(abyss::ParseProcessId(L"0") == 0U, "zero process ID should be rejected");
    Check(abyss::ParseProcessId(L"12x") == 0U, "non-numeric process ID should be rejected");
    Check(abyss::ParseProcessId(L"4294967296") == 0U,
          "process ID larger than a DWORD should be rejected");

    Check(abyss::BuildWatchdogCommand(L"C:\\Program Files\\Abyss\\AbyssCursorHide.exe", 4321) ==
              L"\"C:\\Program Files\\Abyss\\AbyssCursorHide.exe\" --watch 4321",
          "watchdog command should quote the path and pass the process ID");
    Check(abyss::BuildWatchdogCommand(L"C:\\Abyss\\AbyssCursorHide.exe", 0).empty(),
          "watchdog command should reject a zero process ID");
    Check(abyss::BuildWatchdogCommand(L"C:\\bad\"path.exe", 4321).empty(),
          "watchdog command should reject an unsafe path");
    Check(abyss::ParseProcessId(L"4321") == 4321U,
          "watchdog process ID should round-trip through the parser");

    const std::uint8_t enabled_by_default[] = {0x02, 0x00, 0x00, 0x00};
    const std::uint8_t disabled_in_task_manager[] = {0x03, 0x00, 0x00, 0x00};
    const std::uint8_t enabled_alternate[] = {0x06, 0x00, 0x00, 0x00};
    const std::uint8_t disabled_alternate[] = {0x07, 0x00, 0x00, 0x00};
    Check(!abyss::IsStartupApprovalDisabled(enabled_by_default, sizeof(enabled_by_default)),
          "an even StartupApproved flag should mean enabled");
    Check(abyss::IsStartupApprovalDisabled(disabled_in_task_manager,
                                           sizeof(disabled_in_task_manager)),
          "an odd StartupApproved flag should mean disabled");
    Check(!abyss::IsStartupApprovalDisabled(enabled_alternate, sizeof(enabled_alternate)),
          "flag 0x06 should mean enabled");
    Check(abyss::IsStartupApprovalDisabled(disabled_alternate, sizeof(disabled_alternate)),
          "flag 0x07 should mean disabled");
    Check(!abyss::IsStartupApprovalDisabled(nullptr, 0),
          "a missing StartupApproved value should not block startup");

    const abyss::ScreenPoint anchor = abyss::RaiseMenuAnchor({120, 900});
    Check(anchor.x == 120, "menu anchor adjustment should preserve x");
    Check(anchor.y == 892, "menu anchor should move exactly eight pixels upward");
    const abyss::ScreenPoint negative_anchor = abyss::RaiseMenuAnchor({-100, -50});
    Check(negative_anchor.x == -100 && negative_anchor.y == -58,
          "menu anchor should preserve negative multi-monitor coordinates");

    if (g_failures == 0)
    {
        std::cout << "All core tests passed.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
