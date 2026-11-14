#include "abyss/cursor_controller.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace abyss
{
namespace
{

struct CursorTarget
{
    DWORD id;
    bool required;
};

constexpr DWORD kOcrNormal = 32512;
constexpr DWORD kOcrIBeam = 32513;
constexpr DWORD kOcrWait = 32514;
constexpr DWORD kOcrCross = 32515;
constexpr DWORD kOcrUp = 32516;
constexpr DWORD kOcrSizeNwSe = 32642;
constexpr DWORD kOcrSizeNeSw = 32643;
constexpr DWORD kOcrSizeWe = 32644;
constexpr DWORD kOcrSizeNs = 32645;
constexpr DWORD kOcrSizeAll = 32646;
constexpr DWORD kOcrNo = 32648;
constexpr DWORD kOcrHand = 32649;
constexpr DWORD kOcrAppStarting = 32650;
constexpr DWORD kOcrHelp = 32651;

// The obsolete OCR_SIZE (32640) and OCR_ICON (32641) roles are not replaced.
// 5.0.0 replaced them too, and current Windows versions reject OCR_SIZE; the
// rejected cursor was never destroyed, so every hide leaked one. After a few
// thousand hides the app ran out of handles, and its scheme reloads then
// quietly left most cursors, including the link hand, transparent.
constexpr std::array<CursorTarget, 16> kCursorTargets = {{
    {kOcrNormal, true},
    {kOcrIBeam, true},
    {kOcrWait, true},
    {kOcrCross, true},
    {kOcrUp, true},
    {kOcrSizeNwSe, true},
    {kOcrSizeNeSw, true},
    {kOcrSizeWe, true},
    {kOcrSizeNs, true},
    {kOcrSizeAll, true},
    {kOcrNo, true},
    {kOcrHand, true},
    {kOcrAppStarting, true},
    {kOcrHelp, true},
    {32671, false}, // OCR_PIN on supported Windows versions.
    {32672, false}, // OCR_PERSON on supported Windows versions.
}};
static_assert(kCursorTargets.size() == kCursorRoleCount, "one entry per cursor role");
static_assert(kAllCursorRoles == (1U << kCursorTargets.size()) - 1U,
              "kAllCursorRoles must have one bit per cursor role");

// The largest cursor Hide creates; its mask bitmap is twice as tall.
constexpr int kMaxCursorSize = 512;

// A process close to its handle quota cannot load the cursor files when it
// restores the pointer, which is how 5.0.0 left cursors invisible. Refuse to
// hide long before that. The app normally uses a few dozen of each.
constexpr DWORD kMaxGuiObjectsBeforeHide = 1000;

enum class CursorImage
{
    visible,
    transparent,
    unreadable,
};

// A monochrome cursor's mask bitmap holds the AND mask above the XOR mask.
// Transparent means every AND bit is set and every XOR bit is clear.
CursorImage InspectMonochromeMask(const HBITMAP mask_bitmap) noexcept
{
    BITMAP mask{};
    if (mask_bitmap == nullptr || GetObjectW(mask_bitmap, sizeof(mask), &mask) != sizeof(mask) ||
        mask.bmWidth <= 0 || mask.bmHeight < 2 || (mask.bmHeight % 2) != 0 ||
        mask.bmPlanes != 1 || mask.bmBitsPixel != 1)
    {
        return CursorImage::unreadable;
    }

    // Hide never installs anything this big, so it is somebody else's cursor.
    if (mask.bmWidth > kMaxCursorSize || mask.bmHeight > 2 * kMaxCursorSize)
    {
        return CursorImage::visible;
    }

    // GetBitmapBits pads each row to a multiple of 16 bits, whatever kind of
    // bitmap GetIconInfo hands back, so the stride comes from the width.
    const auto width = static_cast<std::size_t>(mask.bmWidth);
    const std::size_t stride = ((width + 15) / 16) * 2;
    const auto height = static_cast<std::size_t>(mask.bmHeight) / 2;
    std::vector<BYTE> bits(stride * height * 2);
    if (GetBitmapBits(mask_bitmap, static_cast<LONG>(bits.size()), bits.data()) !=
        static_cast<LONG>(bits.size()))
    {
        return CursorImage::unreadable;
    }

    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            const auto bit = static_cast<BYTE>(0x80U >> (x % 8));
            const bool and_set = (bits[y * stride + x / 8] & bit) != 0;
            const bool xor_set = (bits[(y + height) * stride + x / 8] & bit) != 0;
            if (!and_set || xor_set)
            {
                return CursorImage::visible;
            }
        }
    }
    return CursorImage::transparent;
}

CursorImage InspectCursor(const HCURSOR cursor) noexcept
{
    ICONINFO info{};
    if (cursor == nullptr || GetIconInfo(cursor, &info) == FALSE)
    {
        return CursorImage::unreadable;
    }

    // The cursors Hide installs are monochrome, so a color cursor is visible.
    const CursorImage image =
        info.hbmColor == nullptr ? InspectMonochromeMask(info.hbmMask) : CursorImage::visible;

    if (info.hbmMask != nullptr)
    {
        DeleteObject(info.hbmMask);
    }
    if (info.hbmColor != nullptr)
    {
        DeleteObject(info.hbmColor);
    }
    return image;
}

bool NearHandleQuota() noexcept
{
    const HANDLE self = GetCurrentProcess();
    return GetGuiResources(self, GR_USEROBJECTS) > kMaxGuiObjectsBeforeHide ||
           GetGuiResources(self, GR_GDIOBJECTS) > kMaxGuiObjectsBeforeHide;
}

} // namespace

bool ReloadSystemCursorScheme() noexcept
{
    const BOOL reloaded = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    return reloaded != FALSE;
}

bool RestoreSystemCursorScheme(const CursorRoleMask expected) noexcept
{
    const bool reloaded = ReloadSystemCursorScheme();
    return reloaded && CursorRolesVisible(expected);
}

CursorRoleMask VisibleCursorRoles() noexcept
{
    CursorRoleMask visible = 0;
    for (std::size_t index = 0; index < kCursorTargets.size(); ++index)
    {
        // Loading a system cursor returns the shared handle and allocates
        // nothing. Roles this Windows version lacks return no handle.
        const HCURSOR cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(kCursorTargets[index].id));
        if (cursor == nullptr || InspectCursor(cursor) == CursorImage::visible)
        {
            visible |= CursorRoleMask{1} << index;
        }
    }
    return visible;
}

bool CursorRolesVisible(const CursorRoleMask roles) noexcept
{
    return (VisibleCursorRoles() & roles) == (roles & kAllCursorRoles);
}

bool IsTransparentCursor(const HCURSOR cursor) noexcept
{
    return InspectCursor(cursor) == CursorImage::transparent;
}

bool ReplaceWithTransparentCursor(const HINSTANCE instance, const DWORD role, const int width,
                                  const int height) noexcept
{
    if (width <= 0 || height <= 0 || width > kMaxCursorSize || height > kMaxCursorSize)
    {
        return false;
    }

    const int bytes_per_row = ((width + 15) / 16) * 2;
    const auto mask_bytes =
        static_cast<std::size_t>(bytes_per_row) * static_cast<std::size_t>(height);
    const std::vector<BYTE> and_mask(mask_bytes, 0xFF);
    const std::vector<BYTE> xor_mask(mask_bytes, 0x00);

    const HCURSOR cursor =
        CreateCursor(instance, 0, 0, width, height, and_mask.data(), xor_mask.data());
    if (cursor == nullptr)
    {
        return false;
    }

    // Windows takes ownership of the cursor only when it accepts the role.
    if (SetSystemCursor(cursor, role) != FALSE)
    {
        return true;
    }
    DestroyCursor(cursor);
    return false;
}

CursorController::CursorController(const HINSTANCE instance) noexcept : instance_(instance)
{
}

CursorController::~CursorController()
{
    if (hidden_)
    {
        (void)Restore();
    }
    ReleaseSavedCursors();
}

// Reloading the scheme cannot bring back a cursor whose file is missing (a
// deleted cursor pack, a file on a disconnected drive) or that this process no
// longer has the handles to load; Windows then keeps the transparent cursor.
// So Hide keeps a copy of every visible cursor, and this puts the copy back
// for each role that is still transparent. Windows takes ownership of a copy
// it accepts, so this needs no new handles. An animated cursor comes back as
// a still image until the next successful reload.
void CursorController::PutBackSavedCursors() noexcept
{
    const CursorRoleMask visible = VisibleCursorRoles();
    bool put_back = false;
    for (std::size_t index = 0; index < kCursorTargets.size(); ++index)
    {
        const CursorRoleMask bit = CursorRoleMask{1} << index;
        HCURSOR& saved = saved_cursors_[index];
        if (saved != nullptr && (expected_roles_ & bit) != 0 && (visible & bit) == 0 &&
            SetSystemCursor(saved, kCursorTargets[index].id) != FALSE)
        {
            saved = nullptr;
            put_back = true;
        }
    }
    if (put_back)
    {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

void CursorController::ReleaseSavedCursors() noexcept
{
    for (HCURSOR& saved : saved_cursors_)
    {
        if (saved != nullptr)
        {
            DestroyCursor(saved);
            saved = nullptr;
        }
    }
}

bool CursorController::Hide() noexcept
{
    if (hidden_)
    {
        return true;
    }

    // The largest accessibility pointer sizes can exceed the cap; a smaller
    // transparent cursor hides just as well.
    const int width = std::min(GetSystemMetrics(SM_CXCURSOR), kMaxCursorSize);
    const int height = std::min(GetSystemMetrics(SM_CYCURSOR), kMaxCursorSize);
    if (width <= 0 || height <= 0 || NearHandleQuota())
    {
        return false;
    }

    expected_roles_ = VisibleCursorRoles();
    ReleaseSavedCursors();
    for (std::size_t index = 0; index < kCursorTargets.size(); ++index)
    {
        if ((expected_roles_ & (CursorRoleMask{1} << index)) != 0)
        {
            const HCURSOR current =
                LoadCursorW(nullptr, MAKEINTRESOURCEW(kCursorTargets[index].id));
            saved_cursors_[index] = current != nullptr ? CopyCursor(current) : nullptr;
        }
    }

    bool all_required_succeeded = true;
    unsigned int success_count = 0;

    for (const CursorTarget target : kCursorTargets)
    {
        if (ReplaceWithTransparentCursor(instance_, target.id, width, height))
        {
            ++success_count;
        }
        else if (target.required)
        {
            all_required_succeeded = false;
        }
    }

    if (!all_required_succeeded || success_count == 0)
    {
        if (!RestoreSystemCursorScheme(expected_roles_))
        {
            PutBackSavedCursors();
        }
        ReleaseSavedCursors();
        return false;
    }

    hidden_ = true;
    SetCursor(nullptr);
    return true;
}

bool CursorController::Restore() noexcept
{
    if (!hidden_)
    {
        return true;
    }

    hidden_ = false;
    bool restored = RestoreSystemCursorScheme(expected_roles_);
    if (!restored)
    {
        PutBackSavedCursors();
        restored = CursorRolesVisible(expected_roles_);
    }
    ReleaseSavedCursors();
    return restored;
}

bool CursorController::hidden() const noexcept
{
    return hidden_;
}

CursorRoleMask CursorController::expected_roles() const noexcept
{
    return expected_roles_;
}

} // namespace abyss
