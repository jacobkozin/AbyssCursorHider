#include "abyss/cursor_controller.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

// These checks never change the system cursors: they only create private
// cursors, and call SetSystemCursor only with a role Windows rejects (checked
// first with a harmless visible cursor).
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

// CreateCursor takes WORD-aligned rows, like CreateBitmap.
std::size_t RowBytes(const int width)
{
    return static_cast<std::size_t>(((width + 15) / 16) * 2);
}

HCURSOR MakeCursor(const int size, const std::vector<BYTE>& and_mask,
                   const std::vector<BYTE>& xor_mask)
{
    return CreateCursor(GetModuleHandleW(nullptr), 0, 0, size, size, and_mask.data(),
                        xor_mask.data());
}

void Destroy(const HCURSOR cursor)
{
    if (cursor != nullptr)
    {
        DestroyCursor(cursor);
    }
}

DWORD UserObjects()
{
    return GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
}

DWORD GdiObjects()
{
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

// 24 and 48 have WORD-aligned rows that are not DWORD-aligned (48 px is the
// cursor size at 150% scaling), so a stride mistake shows up there.
void CheckSize(const int size)
{
    const std::size_t row = RowBytes(size);
    const std::size_t bytes = row * static_cast<std::size_t>(size);
    const std::vector<BYTE> all_set(bytes, 0xFF);
    const std::vector<BYTE> all_clear(bytes, 0x00);

    const HCURSOR transparent = MakeCursor(size, all_set, all_clear);
    Check(transparent != nullptr, "a transparent test cursor should be created");
    Check(abyss::IsTransparentCursor(transparent), "an all-transparent cursor should be detected");
    Destroy(transparent);

    const HCURSOR solid = MakeCursor(size, all_clear, all_clear);
    Check(!abyss::IsTransparentCursor(solid), "a solid black cursor is not transparent");
    Destroy(solid);

    // The last visible pixel of the last row (not a padding bit).
    std::vector<BYTE> last_and = all_set;
    const auto last_x = static_cast<std::size_t>(size - 1);
    last_and[row * static_cast<std::size_t>(size - 1) + last_x / 8] &=
        static_cast<BYTE>(~(0x80U >> (last_x % 8)));
    const HCURSOR last_pixel = MakeCursor(size, last_and, all_clear);
    Check(!abyss::IsTransparentCursor(last_pixel),
          "a cursor with only its last pixel visible is not transparent");
    Destroy(last_pixel);

    // Only padding bits differ: still transparent.
    if (static_cast<std::size_t>(size) < row * 8)
    {
        std::vector<BYTE> padding_xor = all_clear;
        padding_xor[row - 1] = 0x01;
        const HCURSOR padded = MakeCursor(size, all_set, padding_xor);
        Check(abyss::IsTransparentCursor(padded), "padding bits should be ignored");
        Destroy(padded);
    }

    std::vector<BYTE> first_xor = all_clear;
    first_xor[0] = 0x80;
    const HCURSOR inverted_pixel = MakeCursor(size, all_set, first_xor);
    Check(!abyss::IsTransparentCursor(inverted_pixel),
          "a cursor that inverts one pixel is not transparent");
    Destroy(inverted_pixel);
}

} // namespace

int main()
{
    for (const int size : std::array<int, 4>{24, 32, 48, 72})
    {
        CheckSize(size);
    }
    Check(!abyss::IsTransparentCursor(nullptr), "a missing cursor is not transparent");

    // Role 1 is not a cursor role. Prove Windows rejects it with a visible copy
    // of the arrow first, so this test cannot hide anyone's pointer.
    constexpr DWORD kRejectedRole = 1;
    const HCURSOR probe = CopyCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (probe == nullptr)
    {
        Check(false, "a copy of the arrow should be created");
    }
    else if (SetSystemCursor(probe, kRejectedRole) != FALSE)
    {
        // Windows took the copy; nothing became invisible. Put it back.
        (void)SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        Check(false, "Windows accepted role 1; skipping the rejected-role leak check");
    }
    else
    {
        DestroyCursor(probe);

        // Warm up once so any first-call allocation is in the baseline.
        (void)abyss::ReplaceWithTransparentCursor(nullptr, kRejectedRole, 32, 32);
        const DWORD user_before = UserObjects();
        const DWORD gdi_before = GdiObjects();
        bool any_accepted = false;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            any_accepted =
                abyss::ReplaceWithTransparentCursor(nullptr, kRejectedRole, 32, 32) || any_accepted;
        }
        if (any_accepted)
        {
            (void)SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        }
        Check(!any_accepted, "a rejected cursor role should report failure");
        Check(UserObjects() == user_before, "rejected cursor roles should not leak USER objects");
        Check(GdiObjects() == gdi_before, "rejected cursor roles should not leak GDI objects");
    }

    Check(!abyss::ReplaceWithTransparentCursor(nullptr, kRejectedRole, 0, 32),
          "a zero-width cursor should be refused");
    Check(!abyss::ReplaceWithTransparentCursor(nullptr, kRejectedRole, 32, 513),
          "an oversized cursor should be refused");

    if (g_failures == 0)
    {
        std::cout << "All cursor tests passed.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
