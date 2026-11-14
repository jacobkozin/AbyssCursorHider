// Reads the two programs that ship and checks what used to be checked by hand
// before a release: the exploit mitigations, where their DLLs load from, what
// they import, the embedded manifest and the version information. It only
// reads the files; it never runs them.
//
// Usage: abyss_binary_tests <AbyssCursorHide.exe> <RestoreCursor.exe> <version>
//            <release|other> <source directory> <build directory>
#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS
#define IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS 20
#endif
#ifndef IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT
#define IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT 0x0001
#endif

namespace
{

int g_failures = 0;

void Check(const bool condition, const std::string_view program, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << program << ": " << message << '\n';
        ++g_failures;
    }
}

// The file names and paths used here are ASCII; anything else becomes '?'.
std::string Narrow(const std::wstring_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const wchar_t character : text)
    {
        result.push_back(character < 0x80 ? static_cast<char>(character) : '?');
    }
    return result;
}

std::string Lower(std::string text)
{
    for (char& character : text)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

class PeFile
{
  public:
    bool Load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }
        const std::streamoff size = file.tellg();
        if (size <= 0)
        {
            return false;
        }
        bytes_.resize(static_cast<std::size_t>(size));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(bytes_.data()), static_cast<std::streamsize>(size)))
        {
            return false;
        }

        IMAGE_DOS_HEADER dos{};
        if (!Read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
        {
            return false;
        }
        const auto headers_offset = static_cast<std::size_t>(dos.e_lfanew);
        if (!Read(headers_offset, headers_) || headers_.Signature != IMAGE_NT_SIGNATURE ||
            headers_.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            headers_.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            return false;
        }

        const std::size_t sections_offset = headers_offset +
                                            offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                                            headers_.FileHeader.SizeOfOptionalHeader;
        for (std::size_t index = 0; index < headers_.FileHeader.NumberOfSections; ++index)
        {
            IMAGE_SECTION_HEADER section{};
            if (!Read(sections_offset + index * sizeof(section), section))
            {
                return false;
            }
            sections_.push_back(section);
        }
        return true;
    }

    template <typename T> bool Read(const std::size_t offset, T& value) const
    {
        if (offset > bytes_.size() || bytes_.size() - offset < sizeof(T))
        {
            return false;
        }
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return true;
    }

    // The file offset of an address relative to the image base, if it lies in
    // a section's data in the file.
    std::optional<std::size_t> Offset(const DWORD rva) const
    {
        for (const IMAGE_SECTION_HEADER& section : sections_)
        {
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.SizeOfRawData)
            {
                return static_cast<std::size_t>(section.PointerToRawData) +
                       (rva - section.VirtualAddress);
            }
        }
        return std::nullopt;
    }

    template <typename T> bool ReadRva(const DWORD rva, T& value) const
    {
        const std::optional<std::size_t> offset = Offset(rva);
        return offset.has_value() && Read(*offset, value);
    }

    std::string StringAt(const DWORD rva) const
    {
        std::string text;
        const std::optional<std::size_t> offset = Offset(rva);
        for (std::size_t index = offset.value_or(bytes_.size());
             index < bytes_.size() && bytes_[index] != 0 && text.size() < MAX_PATH; ++index)
        {
            text.push_back(static_cast<char>(bytes_[index]));
        }
        return text;
    }

    IMAGE_DATA_DIRECTORY Directory(const std::size_t index) const
    {
        if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            index >= headers_.OptionalHeader.NumberOfRvaAndSizes)
        {
            return {};
        }
        return headers_.OptionalHeader.DataDirectory[index];
    }

    const IMAGE_NT_HEADERS64& headers() const noexcept
    {
        return headers_;
    }

    const std::vector<IMAGE_SECTION_HEADER>& sections() const noexcept
    {
        return sections_;
    }

    const std::vector<unsigned char>& bytes() const noexcept
    {
        return bytes_;
    }

  private:
    std::vector<unsigned char> bytes_;
    IMAGE_NT_HEADERS64 headers_{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
};

void CheckHeaders(const PeFile& pe, const std::string_view name)
{
    const IMAGE_NT_HEADERS64& headers = pe.headers();
    const WORD flags = headers.OptionalHeader.DllCharacteristics;
    Check(headers.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI, name,
          "is a Windows GUI program");
    Check((flags & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) != 0 &&
              (headers.FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) == 0 &&
              pe.Directory(IMAGE_DIRECTORY_ENTRY_BASERELOC).Size != 0,
          name, "uses ASLR and keeps its relocations");
    Check((flags & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA) != 0, name,
          "uses high-entropy ASLR");
    Check((flags & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) != 0, name, "is DEP compatible");
    Check((flags & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0, name,
          "is marked for Control Flow Guard");

    bool writable_code = false;
    for (const IMAGE_SECTION_HEADER& section : pe.sections())
    {
        writable_code = writable_code || ((section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0 &&
                                          (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0);
    }
    Check(!writable_code, name, "has no section that is both writable and executable");
}

void CheckLoadConfig(const PeFile& pe, const std::string_view name)
{
    const IMAGE_DATA_DIRECTORY directory = pe.Directory(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG);
    DWORD size = 0;
    const std::optional<std::size_t> offset =
        directory.VirtualAddress != 0 ? pe.Offset(directory.VirtualAddress) : std::nullopt;
    if (!offset.has_value() || !pe.Read(*offset, size))
    {
        Check(false, name, "has a load configuration");
        return;
    }

    // The structure's own Size says which of its fields the linker wrote.
    IMAGE_LOAD_CONFIG_DIRECTORY64 config{};
    const std::size_t available = std::min<std::size_t>(
        {static_cast<std::size_t>(size), sizeof(config), pe.bytes().size() - *offset});
    std::memcpy(&config, pe.bytes().data() + *offset, available);

    const bool has_load_flags =
        available >= offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, DependentLoadFlags) +
                         sizeof(config.DependentLoadFlags);
    Check(has_load_flags && config.DependentLoadFlags == LOAD_LIBRARY_SEARCH_SYSTEM32, name,
          "resolves its imports from System32 only (dependent load flag 0x800)");

    const bool has_guard_flags = available >= offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardFlags) +
                                                  sizeof(config.GuardFlags);
    Check(has_guard_flags && (config.GuardFlags & IMAGE_GUARD_CF_INSTRUMENTED) != 0 &&
              (config.GuardFlags & IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT) != 0,
          name, "is built with Control Flow Guard checks");
}

void CheckDebugDirectory(const PeFile& pe, const std::string_view name, const bool release)
{
    const IMAGE_DATA_DIRECTORY directory = pe.Directory(IMAGE_DIRECTORY_ENTRY_DEBUG);
    bool cet_compatible = false;
    bool names_debug_database = false;
    constexpr auto kEntrySize = static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY));
    for (DWORD used = 0; directory.VirtualAddress != 0 && used + kEntrySize <= directory.Size;
         used += kEntrySize)
    {
        IMAGE_DEBUG_DIRECTORY entry{};
        if (!pe.ReadRva(directory.VirtualAddress + used, entry))
        {
            break;
        }
        DWORD extended = 0;
        if (entry.Type == IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS &&
            entry.SizeOfData >= sizeof(extended) && pe.Read(entry.PointerToRawData, extended))
        {
            cet_compatible = (extended & IMAGE_DLLCHARACTERISTICS_EX_CET_COMPAT) != 0;
        }
        names_debug_database = names_debug_database || entry.Type == IMAGE_DEBUG_TYPE_CODEVIEW;
    }
    Check(cet_compatible, name, "is marked CET shadow stack compatible");
    if (release)
    {
        Check(!names_debug_database, name, "names no debug database (PDB) path");
    }
}

std::vector<std::string> ImportedDlls(const PeFile& pe)
{
    std::vector<std::string> names;
    const IMAGE_DATA_DIRECTORY directory = pe.Directory(IMAGE_DIRECTORY_ENTRY_IMPORT);
    constexpr auto kEntrySize = static_cast<DWORD>(sizeof(IMAGE_IMPORT_DESCRIPTOR));
    for (DWORD used = 0; directory.VirtualAddress != 0 && used + kEntrySize <= directory.Size;
         used += kEntrySize)
    {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!pe.ReadRva(directory.VirtualAddress + used, descriptor) || descriptor.Name == 0)
        {
            break;
        }
        names.push_back(Lower(pe.StringAt(descriptor.Name)));
    }
    return names;
}

std::vector<std::string> DelayLoadedDlls(const PeFile& pe, bool& all_rva_based)
{
    std::vector<std::string> names;
    const IMAGE_DATA_DIRECTORY directory = pe.Directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    constexpr auto kEntrySize = static_cast<DWORD>(sizeof(IMAGE_DELAYLOAD_DESCRIPTOR));
    for (DWORD used = 0; directory.VirtualAddress != 0 && used + kEntrySize <= directory.Size;
         used += kEntrySize)
    {
        IMAGE_DELAYLOAD_DESCRIPTOR descriptor{};
        if (!pe.ReadRva(directory.VirtualAddress + used, descriptor) || descriptor.DllNameRVA == 0)
        {
            break;
        }
        all_rva_based = all_rva_based && descriptor.Attributes.RvaBased != 0;
        names.push_back(Lower(pe.StringAt(descriptor.DllNameRVA)));
    }
    return names;
}

// Windows maps KnownDLLs from System32 no matter where a program starts, and
// API sets are resolved without a file search. Any other DLL imported directly
// could be found next to the program before wWinMain can restrict loading.
bool IsKnownDll(const std::string& dll)
{
    // Windows loads these into every process before it resolves the program's
    // imports; Windows Server does not list kernel32 under KnownDLLs.
    constexpr std::array<std::string_view, 3> kAlwaysLoaded = {
        "ntdll.dll", "kernel32.dll", "kernelbase.dll"};
    if (std::find(kAlwaysLoaded.begin(), kAlwaysLoaded.end(), dll) != kAlwaysLoaded.end() ||
        dll.rfind("api-ms-win-", 0) == 0 || dll.rfind("ext-ms-", 0) == 0)
    {
        return true;
    }
    const std::string base = dll.substr(0, dll.rfind('.'));
    const std::wstring value_name(base.begin(), base.end());
    wchar_t data[MAX_PATH]{};
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KnownDLLs",
                     value_name.c_str(), RRF_RT_REG_SZ, nullptr, data, &size) != ERROR_SUCCESS)
    {
        return false;
    }
    return Lower(Narrow(data)) == dll;
}

void CheckImports(const PeFile& pe, const std::string_view name,
                  const std::vector<std::string>& expected_delay_loaded)
{
    constexpr std::array<std::string_view, 15> kNetworkDlls = {
        "ws2_32.dll",   "wsock32.dll", "mswsock.dll", "wininet.dll", "winhttp.dll",
        "urlmon.dll",   "iphlpapi.dll", "dnsapi.dll", "netapi32.dll", "wldap32.dll",
        "httpapi.dll", "webio.dll",   "websocket.dll", "rasapi32.dll", "mpr.dll",
    };
    const auto is_network = [&kNetworkDlls](const std::string& dll) {
        return std::find(kNetworkDlls.begin(), kNetworkDlls.end(), dll) != kNetworkDlls.end();
    };

    const std::vector<std::string> imports = ImportedDlls(pe);
    Check(!imports.empty(), name, "has an import table");
    for (const std::string& dll : imports)
    {
        Check(IsKnownDll(dll), name, "imports " + dll + " directly, which is not a KnownDLL");
        Check(!is_network(dll), name, "imports the networking library " + dll);
        Check(std::find(expected_delay_loaded.begin(), expected_delay_loaded.end(), dll) ==
                  expected_delay_loaded.end(),
              name, "imports " + dll + " directly instead of delay-loading it");
    }

    bool all_rva_based = true;
    std::vector<std::string> delay_loaded = DelayLoadedDlls(pe, all_rva_based);
    Check(all_rva_based, name, "uses the current delay-load table format");
    for (const std::string& dll : delay_loaded)
    {
        Check(!is_network(dll), name, "delay-loads the networking library " + dll);
    }
    std::vector<std::string> expected = expected_delay_loaded;
    std::sort(delay_loaded.begin(), delay_loaded.end());
    std::sort(expected.begin(), expected.end());
    Check(delay_loaded == expected, name, "delay-loads exactly the expected DLLs");
}

std::optional<std::array<DWORD, 3>> ParseVersion(const std::wstring& text)
{
    std::array<DWORD, 3> parts{};
    const wchar_t* cursor = text.c_str();
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        wchar_t* end = nullptr;
        const unsigned long value = std::wcstoul(cursor, &end, 10);
        const wchar_t expected_end = index + 1 < parts.size() ? L'.' : L'\0';
        if (end == cursor || *end != expected_end || value > 0xFFFFUL)
        {
            return std::nullopt;
        }
        parts[index] = static_cast<DWORD>(value);
        cursor = end + 1;
    }
    return parts;
}

void CheckVersionInfo(const std::wstring& path, const std::string_view name,
                      const std::wstring& file_name, const std::wstring& version,
                      const std::array<DWORD, 3>& parts)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<unsigned char> block(size);
    if (size == 0 || GetFileVersionInfoW(path.c_str(), 0, size, block.data()) == FALSE)
    {
        Check(false, name, "has version information");
        return;
    }

    void* fixed_data = nullptr;
    UINT fixed_size = 0;
    VS_FIXEDFILEINFO fixed{};
    if (VerQueryValueW(block.data(), L"\\", &fixed_data, &fixed_size) != FALSE &&
        fixed_data != nullptr && fixed_size >= sizeof(fixed))
    {
        std::memcpy(&fixed, fixed_data, sizeof(fixed));
    }
    const DWORD expected_high = (parts[0] << 16) | parts[1];
    const DWORD expected_low = parts[2] << 16;
    Check(fixed.dwSignature == VS_FFI_SIGNATURE && fixed.dwFileVersionMS == expected_high &&
              fixed.dwFileVersionLS == expected_low && fixed.dwProductVersionMS == expected_high &&
              fixed.dwProductVersionLS == expected_low,
          name, "file and product versions match the project version");

    void* translation_data = nullptr;
    UINT translation_size = 0;
    std::array<WORD, 2> translation{};
    if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", &translation_data,
                       &translation_size) == FALSE ||
        translation_data == nullptr || translation_size < sizeof(translation))
    {
        Check(false, name, "has a version translation table");
        return;
    }
    std::memcpy(translation.data(), translation_data, sizeof(translation));
    wchar_t prefix[64]{};
    (void)swprintf_s(prefix, L"\\StringFileInfo\\%04x%04x\\", translation[0], translation[1]);

    const auto value = [&block, &prefix](const wchar_t* key) {
        const std::wstring sub_block = std::wstring(prefix) + key;
        void* data = nullptr;
        UINT length = 0;
        if (VerQueryValueW(block.data(), sub_block.c_str(), &data, &length) == FALSE ||
            data == nullptr || length == 0)
        {
            return std::wstring();
        }
        const auto* const text = static_cast<const wchar_t*>(data);
        return std::wstring(text, wcsnlen(text, length));
    };

    Check(value(L"ProductName") == L"Abyss Cursor Hide", name,
          "the product name is Abyss Cursor Hide");
    Check(value(L"FileVersion") == version && value(L"ProductVersion") == version, name,
          "the version strings match the project version");
    Check(_wcsicmp(value(L"OriginalFilename").c_str(), file_name.c_str()) == 0, name,
          "the original file name matches the file");
    Check(!value(L"FileDescription").empty() && !value(L"CompanyName").empty(), name,
          "has a description and a company name");
    Check(value(L"LegalCopyright").find(L"MIT License") != std::wstring::npos, name,
          "the copyright names the MIT License");
}

void CheckManifest(const std::wstring& path, const std::string_view name,
                   const std::wstring& version)
{
    std::string manifest;
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module != nullptr)
    {
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
        const HGLOBAL loaded = resource != nullptr ? LoadResource(module, resource) : nullptr;
        const void* const data = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (data != nullptr)
        {
            manifest.assign(static_cast<const char*>(data), SizeofResource(module, resource));
        }
        FreeLibrary(module);
    }
    Check(!manifest.empty(), name, "embeds its manifest");

    // The first assemblyIdentity is the program's own; dependencies come later.
    std::string manifest_version;
    const std::size_t identity = manifest.find("<assemblyIdentity");
    const std::size_t attribute =
        identity == std::string::npos ? std::string::npos : manifest.find("version=\"", identity);
    if (attribute != std::string::npos)
    {
        const std::size_t start = attribute + std::strlen("version=\"");
        const std::size_t end = manifest.find('"', start);
        if (end != std::string::npos)
        {
            manifest_version = manifest.substr(start, end - start);
        }
    }
    Check(manifest_version == Narrow(version) + ".0", name,
          "the manifest version matches the project version (resources/app.manifest is "
          "updated by hand)");
    Check(manifest.find("level=\"asInvoker\"") != std::string::npos &&
              manifest.find("uiAccess=\"false\"") != std::string::npos,
          name, "the manifest runs it as the invoking user");
    Check(manifest.find("PerMonitorV2") != std::string::npos, name,
          "the manifest declares per-monitor V2 DPI awareness");
}

// Looks for each directory with either kind of slash, in ANSI and UTF-16,
// ignoring ASCII case.
void CheckNoBuildPaths(const PeFile& pe, const std::string_view name,
                       const std::vector<std::wstring>& directories)
{
    const auto lower_byte = [](const unsigned char byte) {
        return byte >= 'A' && byte <= 'Z' ? static_cast<unsigned char>(byte - 'A' + 'a') : byte;
    };
    std::vector<unsigned char> haystack(pe.bytes().size());
    std::transform(pe.bytes().begin(), pe.bytes().end(), haystack.begin(), lower_byte);

    bool found = false;
    for (const std::wstring& directory : directories)
    {
        for (const wchar_t separator : {L'/', L'\\'})
        {
            std::vector<unsigned char> ansi;
            std::vector<unsigned char> utf16;
            for (wchar_t character : directory)
            {
                if (character == L'/' || character == L'\\')
                {
                    character = separator;
                }
                const auto low = lower_byte(static_cast<unsigned char>(character & 0xFF));
                ansi.push_back(low);
                utf16.push_back(low);
                utf16.push_back(static_cast<unsigned char>((character >> 8) & 0xFF));
            }
            for (const std::vector<unsigned char>* needle : {&ansi, &utf16})
            {
                found = found || (!needle->empty() &&
                                  std::search(haystack.begin(), haystack.end(), needle->begin(),
                                              needle->end()) != haystack.end());
            }
        }
    }
    Check(!found, name, "contains no path from the build machine");
}

struct Expectations
{
    std::wstring version;
    std::array<DWORD, 3> version_parts;
    bool release;
    std::vector<std::wstring> build_directories;
};

void CheckProgram(const std::wstring& path, const std::vector<std::string>& delay_loaded,
                  const Expectations& expect)
{
    const std::wstring file_name = std::filesystem::path(path).filename().wstring();
    const std::string name = Narrow(file_name);

    PeFile pe;
    if (!pe.Load(path))
    {
        Check(false, name, "is a 64-bit Windows program that can be read");
        return;
    }
    CheckHeaders(pe, name);
    CheckLoadConfig(pe, name);
    CheckDebugDirectory(pe, name, expect.release);
    CheckImports(pe, name, delay_loaded);
    CheckVersionInfo(path, name, file_name, expect.version, expect.version_parts);
    CheckManifest(path, name, expect.version);
    if (expect.release)
    {
        CheckNoBuildPaths(pe, name, expect.build_directories);
    }
}

} // namespace

int wmain(const int argc, wchar_t** const argv)
{
    if (argc != 7)
    {
        std::cerr << "usage: abyss_binary_tests <AbyssCursorHide.exe> <RestoreCursor.exe> "
                     "<version> <release|other> <source directory> <build directory>\n";
        return 2;
    }

    const std::wstring version = argv[3];
    const std::optional<std::array<DWORD, 3>> parts = ParseVersion(version);
    if (!parts.has_value())
    {
        std::cerr << "FAIL: the version argument is not major.minor.patch\n";
        return 1;
    }
    const Expectations expect{version, *parts, std::wstring_view(argv[4]) == L"release",
                              {argv[5], argv[6]}};

    // wtsapi32.dll is the app's only import that is not a KnownDLL.
    CheckProgram(argv[1], {"wtsapi32.dll"}, expect);
    CheckProgram(argv[2], {}, expect);

    if (g_failures == 0)
    {
        std::cout << "All binary checks passed" << (expect.release ? " (Release)" : "") << ".\n";
    }
    return g_failures == 0 ? 0 : 1;
}
