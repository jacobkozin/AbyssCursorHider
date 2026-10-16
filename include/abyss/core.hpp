#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace abyss
{

inline constexpr std::uint32_t kDefaultDelaySeconds = 3;
inline constexpr int kMenuAnchorOffsetPixels = 8;
inline constexpr std::array<std::uint32_t, 8> kDelayOptionsSeconds = {
    1, 2, 3, 5, 10, 15, 30, 60,
};

struct AppSettings
{
    bool enabled = true;
    std::uint32_t delay_seconds = kDefaultDelaySeconds;
};

struct ScreenPoint
{
    int x = 0;
    int y = 0;
};

[[nodiscard]] bool IsAllowedDelay(std::uint32_t seconds) noexcept;
[[nodiscard]] AppSettings NormalizeSettings(AppSettings settings) noexcept;
[[nodiscard]] ScreenPoint RaiseMenuAnchor(ScreenPoint point) noexcept;
[[nodiscard]] std::uint32_t ParseProcessId(std::wstring_view text) noexcept;

// Returns an empty string when the path cannot be represented safely as a
// Windows Run-key command.
[[nodiscard]] std::wstring BuildStartupCommand(std::wstring_view executable_path);

} // namespace abyss
