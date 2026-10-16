#pragma once

#include "abyss/core.hpp"

#include <string>

namespace abyss
{

class SettingsStore final
{
  public:
    [[nodiscard]] AppSettings Load() const noexcept;
    [[nodiscard]] bool Save(const AppSettings& settings) const noexcept;
};

class StartupRegistration final
{
  public:
    explicit StartupRegistration(std::wstring executable_path);

    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] bool SetEnabled(bool enabled) const noexcept;

  private:
    std::wstring executable_path_;
    std::wstring command_;
};

[[nodiscard]] std::wstring CurrentExecutablePath();
[[nodiscard]] std::wstring CurrentExecutableDirectory();

} // namespace abyss
