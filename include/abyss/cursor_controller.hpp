#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace abyss
{

// Bit i stands for the i-th cursor role the app replaces when it hides the
// pointer (the arrow, text beam, hand and so on; see cursor_controller.cpp).
using CursorRoleMask = std::uint32_t;
inline constexpr std::size_t kCursorRoleCount = 16;
inline constexpr CursorRoleMask kAllCursorRoles = 0xFFFFU;

// Reloads the user's cursor scheme with SPI_SETCURSORS and returns what Windows
// reports. Windows loads the scheme's files inside the calling process and
// quietly skips any it cannot load, for example when that process is out of
// handles, yet still reports success.
[[nodiscard]] bool ReloadSystemCursorScheme() noexcept;

// Reloads the scheme and reports whether every role in `expected` is visible
// afterwards.
[[nodiscard]] bool RestoreSystemCursorScheme(CursorRoleMask expected) noexcept;

// The roles whose current system cursor is visible. A role this version of
// Windows does not have counts as visible, since there is nothing to restore;
// a cursor that cannot be read counts as not visible.
[[nodiscard]] CursorRoleMask VisibleCursorRoles() noexcept;

// True for a monochrome cursor whose image is completely transparent, which is
// what Hide installs.
[[nodiscard]] bool IsTransparentCursor(HCURSOR cursor) noexcept;

// Replaces one system cursor role with a new transparent cursor of the given
// size. Returns false when the cursor cannot be created or Windows rejects the
// role; nothing is left allocated either way.
[[nodiscard]] bool ReplaceWithTransparentCursor(HINSTANCE instance, DWORD role, int width,
                                                int height) noexcept;

class CursorController final
{
  public:
    explicit CursorController(HINSTANCE instance) noexcept;
    ~CursorController();

    CursorController(const CursorController&) = delete;
    CursorController& operator=(const CursorController&) = delete;

    // Replaces every cursor role with a transparent cursor. Returns false when
    // nothing was hidden; any roles already replaced are reloaded again, and
    // CursorRolesVisible(expected_roles()) tells whether that worked.
    [[nodiscard]] bool Hide() noexcept;

    // Reloads the cursor scheme if this controller hid the pointer, and puts
    // back the copy Hide kept of any cursor the reload left transparent.
    // Returns false when a role that was visible before the hide is still
    // transparent afterwards; the controller no longer counts as hidden either
    // way, because repeating the same steps in this process cannot fix that.
    [[nodiscard]] bool Restore() noexcept;
    [[nodiscard]] bool hidden() const noexcept;

    // The roles that were visible just before the last hide, which a restore
    // must bring back. A role the user's own scheme leaves blank is not in it.
    [[nodiscard]] CursorRoleMask expected_roles() const noexcept;

  private:
    void PutBackSavedCursors() noexcept;
    void ReleaseSavedCursors() noexcept;

    HINSTANCE instance_ = nullptr;
    bool hidden_ = false;
    CursorRoleMask expected_roles_ = kAllCursorRoles;
    // Copies of the cursors that were visible just before the last hide.
    std::array<HCURSOR, kCursorRoleCount> saved_cursors_{};
};

// True when every role in `roles` is visible right now.
[[nodiscard]] bool CursorRolesVisible(CursorRoleMask roles) noexcept;

} // namespace abyss
