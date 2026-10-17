#include "oodle_decoder.h"

#include <array>
#include <new>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace ff7rp::pipeline {
namespace {

Status invalid(const char* message)
{
    return Status::error(StatusCode::InvalidArgument, message);
}

std::intptr_t invoke(detail::OodleDecompressFunction function,
    const std::uint8_t* input, std::size_t input_bytes, std::uint8_t* output, std::size_t output_bytes)
{
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
#endif
        return function(input, input_bytes, output, output_bytes, 1, 1, 0, 0, 0, 0, 0, nullptr, 0, 3);
#if defined(_WIN32) && defined(_MSC_VER)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
#endif
}

#ifdef _WIN32
// Tools/ThirdParty/retoc-ff7r/README.md; repak's pinned Windows Oodle 2.9.10.
constexpr std::array<std::uint8_t, 32> kCompanionSha256{
    0x6f,0x5d,0x41,0xa7,0x89,0x2e,0xa6,0xb2,0xdb,0x42,0x0f,0x24,0x58,0xda,0xd2,0xf8,
    0x4a,0x63,0x90,0x1c,0x9a,0x93,0xce,0x94,0x97,0x33,0x7b,0x16,0xc1,0x95,0xf4,0x57};

Status io_error(const char* stage)
{
    return Status::error(StatusCode::IoError,
        std::string(stage) + " (Windows error " + std::to_string(GetLastError()) + ")");
}

struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~Hash()
    {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

Status authenticate(HANDLE file)
{
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length)) return io_error("Oodle file size");
    if (length.QuadPart <= 0 || length.QuadPart > 16 * 1024 * 1024) return invalid("Oodle DLL size bound");
    Hash state;
    if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(state.algorithm, &state.hash, nullptr, 0, nullptr, 0, 0) < 0)
        return Status::error(StatusCode::IoError, "Oodle SHA256 initialization failed");
    std::array<std::uint8_t, 64 * 1024> buffer{};
    LONGLONG total = 0;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            return io_error("Oodle DLL read");
        if (!count) break;
        total += count;
        if (total > length.QuadPart || BCryptHashData(state.hash, buffer.data(), count, 0) < 0)
            return Status::error(StatusCode::IoError, "Oodle SHA256 read failed");
    }
    std::array<std::uint8_t, 32> digest{};
    if (total != length.QuadPart || BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        return Status::error(StatusCode::IoError, "Oodle SHA256 completion failed");
    if (digest != kCompanionSha256) return invalid("Oodle companion SHA256 mismatch");
    return Status::ok_status();
}
#endif
} // namespace

Status detail::decode_oodle_block(OodleDecompressFunction function,
    std::span<const std::uint8_t> input, std::size_t decoded_bytes, std::vector<std::uint8_t>* output)
{
    if (!output) return invalid("Oodle output is null");
    // Check overlap before clearing output, which would invalidate an aliased input.
    const auto in = reinterpret_cast<std::uintptr_t>(input.data());
    const auto out = reinterpret_cast<std::uintptr_t>(output->data());
    const bool alias = !input.empty() && !output->empty() &&
        (in >= out ? in - out < output->size() : out - in < input.size());
    output->clear();
    if (alias) return invalid("Oodle input/output overlap");
    if (!input.data() || input.empty() || input.size() > kOodleMaximumBlockBytes ||
        decoded_bytes == 0 || decoded_bytes > kOodleMaximumBlockBytes)
        return invalid("Oodle block size bound");
    if (!function) return invalid("Oodle decoder unavailable");
    try {
        std::vector<std::uint8_t> decoded(decoded_bytes);
        const auto count = invoke(function, input.data(), input.size(), decoded.data(), decoded.size());
        if (count < 0 || static_cast<std::size_t>(count) != decoded_bytes)
            return invalid("Oodle decompression failed or byte count mismatched");
        *output = std::move(decoded);
    } catch (const std::bad_alloc&) {
        return Status::error(StatusCode::IoError, "Oodle block allocation failed");
    }
    return Status::ok_status();
}

struct OodleDecoder::Module {
    detail::OodleDecompressFunction function = nullptr;
#ifdef _WIN32
    HMODULE library = nullptr;
    // Deny replacement of the file and every directory component until unload.
    std::vector<HANDLE> files;
    ~Module()
    {
        if (library) FreeLibrary(library);
        for (auto file : files) CloseHandle(file);
    }
#endif
};

Status OodleDecoder::from_verified_function(detail::OodleDecompressFunction function, OodleDecoder* decoder)
{
    if (!decoder) return invalid("Oodle decoder output is null");
    decoder->module_.reset();
    if (!function) return invalid("Verified Oodle function is null");
    try {
        auto module = std::make_shared<Module>();
        module->function = function;
        decoder->module_ = std::move(module);
        return Status::ok_status();
    } catch (const std::bad_alloc&) {
        return Status::error(StatusCode::IoError, "Oodle decoder allocation failed");
    }
}

Status OodleDecoder::load(const std::filesystem::path& companion, OodleDecoder* decoder)
{
    if (!decoder) return invalid("Oodle decoder output is null");
    decoder->module_.reset();
#ifndef _WIN32
    (void)companion;
    return invalid("Oodle companion loading is unsupported on this platform (Windows x64 required)");
#else
    if constexpr (sizeof(void*) != 8) return invalid("Oodle requires Windows x64");
    auto path = companion;
    path.make_preferred();
    const auto text = path.native();
    if (!path.is_absolute() || text.size() < 3 || text.size() >= 32768 ||
        text[1] != L':' || text.find(L'\0') != std::wstring::npos || text.find(L':', 2) != std::wstring::npos ||
        _wcsicmp(path.filename().c_str(), L"oo2core_9_win64.dll") != 0)
        return invalid("Oodle requires an explicit absolute local companion path");
    try {
        auto module = std::make_shared<Module>();
        std::filesystem::path current = path.root_path();
        std::vector<std::filesystem::path> components{current};
        for (const auto& part : path.relative_path()) {
            const auto name = part.native();
            if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
                return invalid("Oodle path component is not plain");
            current /= part;
            components.push_back(current);
        }
        module->files.reserve(components.size());
        for (std::size_t i = 0; i < components.size(); ++i) {
            const bool leaf = i + 1 == components.size();
            HANDLE file = CreateFileW(components[i].c_str(), leaf ? GENERIC_READ : FILE_READ_ATTRIBUTES,
                leaf ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (file == INVALID_HANDLE_VALUE) return io_error("Oodle plain path open");
            module->files.push_back(file);
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes, sizeof(attributes)))
                return io_error("Oodle path attributes");
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == leaf ||
                (leaf && GetFileType(file) != FILE_TYPE_DISK))
                return invalid("Oodle path must contain only plain directories and a regular file");
            std::array<wchar_t, 32768> final_path{};
            const auto count = GetFinalPathNameByHandleW(file, final_path.data(),
                static_cast<DWORD>(final_path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            const auto expected = L"\\\\?\\" + components[i].native();
            if (!count || count >= final_path.size() || _wcsicmp(final_path.data(), expected.c_str()) != 0)
                return invalid("Oodle path resolves outside supplied plain path");
        }
        auto status = authenticate(module->files.back());
        if (!status.ok()) return status;
        // Disk authentication cannot vouch for an image mapped before these file locks.
        // Initial loading must be serialized against independent companion loaders.
        if (GetModuleHandleW(path.c_str()))
            return invalid("Oodle companion is already loaded; copy the authenticated decoder instead");
        if (GetLastError() != ERROR_MOD_NOT_FOUND) return io_error("Oodle preexisting module check");
        module->library = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module->library) return io_error("Oodle LoadLibraryExW");
        std::array<wchar_t, 32768> loaded{};
        const DWORD count = GetModuleFileNameW(module->library, loaded.data(), static_cast<DWORD>(loaded.size()));
        if (!count || count == loaded.size() || _wcsicmp(loaded.data(), path.c_str()) != 0)
            return invalid("Oodle loaded module path differs from authenticated file");
        const auto function = GetProcAddress(module->library, "OodleLZ_Decompress");
        MEMORY_BASIC_INFORMATION memory{};
        if (!function || !VirtualQuery(reinterpret_cast<const void*>(function), &memory, sizeof(memory)) ||
            memory.AllocationBase != module->library)
            return invalid("OodleLZ_Decompress is missing or forwarded outside authenticated module");
        module->function = reinterpret_cast<detail::OodleDecompressFunction>(function);
        decoder->module_ = std::move(module);
        return Status::ok_status();
    } catch (const std::bad_alloc&) {
        return Status::error(StatusCode::IoError, "Oodle loader allocation failed");
    }
#endif
}

Status OodleDecoder::decompress(std::span<const std::uint8_t> input, std::size_t decoded_bytes,
    std::vector<std::uint8_t>* output) const
{
    const auto keep_alive = module_;
    return detail::decode_oodle_block(keep_alive ? keep_alive->function : nullptr, input, decoded_bytes, output);
}

} // namespace ff7rp::pipeline
