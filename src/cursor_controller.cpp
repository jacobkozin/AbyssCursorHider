#include "abyss/cursor_controller.hpp"

#include <array>
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
constexpr DWORD kOcrSize = 32640;
constexpr DWORD kOcrIcon = 32641;
constexpr DWORD kOcrSizeNwSe = 32642;
constexpr DWORD kOcrSizeNeSw = 32643;
constexpr DWORD kOcrSizeWe = 32644;
constexpr DWORD kOcrSizeNs = 32645;
constexpr DWORD kOcrSizeAll = 32646;
constexpr DWORD kOcrNo = 32648;
constexpr DWORD kOcrHand = 32649;
constexpr DWORD kOcrAppStarting = 32650;
constexpr DWORD kOcrHelp = 32651;

constexpr std::array<CursorTarget, 18> kCursorTargets = {{
    {kOcrNormal, true},
    {kOcrIBeam, true},
    {kOcrWait, true},
    {kOcrCross, true},
    {kOcrUp, true},
    {kOcrSize, false},
    {kOcrIcon, false},
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

} // namespace

bool RestoreSystemCursorScheme() noexcept
{
    const BOOL restored = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    return restored != FALSE;
}

CursorController::CursorController(const HINSTANCE instance) noexcept : instance_(instance)
{
}

CursorController::~CursorController()
{
    if (hidden_)
    {
        (void)RestoreSystemCursorScheme();
    }
}

bool CursorController::Hide() noexcept
{
    if (hidden_)
    {
        return true;
    }

    const int width = GetSystemMetrics(SM_CXCURSOR);
    const int height = GetSystemMetrics(SM_CYCURSOR);
    if (width <= 0 || height <= 0 || width > 512 || height > 512)
    {
        return false;
    }

    const int bytes_per_row = ((width + 15) / 16) * 2;
    const auto mask_bytes =
        static_cast<std::size_t>(bytes_per_row) * static_cast<std::size_t>(height);
    if (mask_bytes == 0 || mask_bytes > 131072)
    {
        return false;
    }

    const std::vector<BYTE> and_mask(mask_bytes, 0xFF);
    const std::vector<BYTE> xor_mask(mask_bytes, 0x00);

    bool all_required_succeeded = true;
    unsigned int success_count = 0;

    for (const CursorTarget target : kCursorTargets)
    {
        const HCURSOR cursor =
            CreateCursor(instance_, 0, 0, width, height, and_mask.data(), xor_mask.data());
        if (cursor == nullptr)
        {
            all_required_succeeded = all_required_succeeded && !target.required;
            continue;
        }

        if (SetSystemCursor(cursor, target.id) != FALSE)
        {
            ++success_count;
        }
        else if (target.required)
        {
            all_required_succeeded = false;
        }
        // SetSystemCursor owns and destroys the cursor passed to it.
    }

    if (!all_required_succeeded || success_count == 0)
    {
        (void)RestoreSystemCursorScheme();
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

    if (!RestoreSystemCursorScheme())
    {
        return false;
    }

    hidden_ = false;
    return true;
}

bool CursorController::hidden() const noexcept
{
    return hidden_;
}

} // namespace abyss
