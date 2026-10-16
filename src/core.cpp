#include "abyss/core.hpp"

#include <algorithm>
#include <limits>

namespace abyss
{

bool IsAllowedDelay(const std::uint32_t seconds) noexcept
{
    return std::find(kDelayOptionsSeconds.begin(), kDelayOptionsSeconds.end(), seconds) !=
           kDelayOptionsSeconds.end();
}

AppSettings NormalizeSettings(AppSettings settings) noexcept
{
    if (!IsAllowedDelay(settings.delay_seconds))
    {
        settings.delay_seconds = kDefaultDelaySeconds;
    }
    return settings;
}

ScreenPoint RaiseMenuAnchor(ScreenPoint point) noexcept
{
    point.y -= kMenuAnchorOffsetPixels;
    return point;
}

std::uint32_t ParseProcessId(const std::wstring_view text) noexcept
{
    if (text.empty())
    {
        return 0;
    }

    std::uint64_t value = 0;
    for (const wchar_t character : text)
    {
        if (character < L'0' || character > L'9')
        {
            return 0;
        }
        value = value * 10U + static_cast<std::uint64_t>(character - L'0');
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            return 0;
        }
    }

    return value == 0 ? 0 : static_cast<std::uint32_t>(value);
}

std::wstring BuildStartupCommand(const std::wstring_view executable_path)
{
    if (executable_path.empty() || executable_path.find(L'"') != std::wstring_view::npos)
    {
        return {};
    }

    std::wstring command;
    command.reserve(executable_path.size() + 2);
    command.push_back(L'"');
    command.append(executable_path);
    command.push_back(L'"');
    return command;
}

} // namespace abyss
