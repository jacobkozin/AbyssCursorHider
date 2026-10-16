#pragma once

#include <windows.h>

namespace abyss
{

[[nodiscard]] bool RestoreSystemCursorScheme() noexcept;

class CursorController final
{
  public:
    explicit CursorController(HINSTANCE instance) noexcept;
    ~CursorController();

    CursorController(const CursorController&) = delete;
    CursorController& operator=(const CursorController&) = delete;

    [[nodiscard]] bool Hide() noexcept;
    [[nodiscard]] bool Restore() noexcept;
    [[nodiscard]] bool hidden() const noexcept;

  private:
    HINSTANCE instance_ = nullptr;
    bool hidden_ = false;
};

} // namespace abyss
