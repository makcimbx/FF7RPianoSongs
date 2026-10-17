#include "pipeline/oodle_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define OODLE_TEST_CALL __stdcall
#else
#define OODLE_TEST_CALL
#endif

namespace {
using namespace ff7rp::pipeline;
int calls = 0;
bool arguments_valid = true;
std::intptr_t return_count = 4;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::intptr_t OODLE_TEST_CALL fake_decode(const std::uint8_t* input, std::size_t input_bytes,
    std::uint8_t* output, std::size_t output_bytes, std::uint32_t fuzz, std::uint32_t crc,
    std::uint32_t verbosity, std::uint64_t base, std::size_t base_bytes,
    std::uint64_t callback, std::uint64_t user, std::uint8_t* memory,
    std::size_t memory_bytes, std::uint32_t phase)
{
    ++calls;
    arguments_valid &= input && input_bytes > 0 && output && output_bytes > 0 &&
        fuzz == 1 && crc == 1 && verbosity == 0 && base == 0 && base_bytes == 0 &&
        callback == 0 && user == 0 && !memory && memory_bytes == 0 && phase == 3;
    std::fill_n(output, output_bytes, std::uint8_t{0x5a});
    if (input_bytes < 4) return -1;
    return return_count;
}

std::vector<std::uint8_t> read_block(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    check(size > 0 && size <= kOodleMaximumBlockBytes, "optional fixture size bound");
    std::ifstream stream(path, std::ios::binary);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    check(static_cast<bool>(stream.read(reinterpret_cast<char*>(data.data()), data.size())), "fixture read");
    check(stream.peek() == std::char_traits<char>::eof(), "fixture changed length");
    return data;
}

void seam_tests()
{
    const std::array<std::uint8_t, 4> input{1, 2, 3, 4};
    std::vector<std::uint8_t> output{9};
    check(detail::decode_oodle_block(fake_decode, input, 4, &output).ok() &&
        output == std::vector<std::uint8_t>(4, 0x5a) && arguments_valid && calls == 1,
        "pinned ABI arguments and exact successful output");
    for (auto count : {std::intptr_t{-1}, std::intptr_t{0}, std::intptr_t{3}, std::intptr_t{5},
            std::numeric_limits<std::intptr_t>::max()}) {
        return_count = count;
        output = {9};
        check(!detail::decode_oodle_block(fake_decode, input, 4, &output).ok() && output.empty(),
            "failed/mismatched return leaked partial output");
    }
    return_count = 4;
    check(!detail::decode_oodle_block(fake_decode, std::span(input).first(3), 4, &output).ok() && output.empty(),
        "truncated stream accepted");
    const auto before = calls;
    for (auto size : {std::size_t{0}, kOodleMaximumBlockBytes + 1, std::numeric_limits<std::size_t>::max()}) {
        output = {9};
        check(!detail::decode_oodle_block(fake_decode, input, size, &output).ok() && output.empty(), "output bound");
    }
    std::vector<std::uint8_t> oversized(kOodleMaximumBlockBytes + 1, 1);
    check(!detail::decode_oodle_block(fake_decode, oversized, 4, &output).ok(), "input bound");
    check(!detail::decode_oodle_block(fake_decode, {}, 4, &output).ok(), "empty input");
    check(!detail::decode_oodle_block(fake_decode, input, 4, nullptr).ok(), "null output");
    check(!detail::decode_oodle_block(nullptr, input, 4, &output).ok(), "missing C seam");
    for (std::size_t offset : {0u, 1u}) {
        output = {1, 2, 3, 4};
        auto alias = std::span<const std::uint8_t>(output).subspan(offset);
        check(!detail::decode_oodle_block(fake_decode, alias, 4, &output).ok() && output.empty(), "aliased buffer");
    }
    check(calls == before, "rejected arguments called C decoder");
    return_count = static_cast<std::intptr_t>(kOodleMaximumBlockBytes);
    check(detail::decode_oodle_block(fake_decode, std::span(oversized).first(kOodleMaximumBlockBytes),
        kOodleMaximumBlockBytes, &output).ok() && output.size() == kOodleMaximumBlockBytes, "maximum block rejected");
    OodleDecoder empty;
    check(!empty.available() && !empty.decompress(input, 4, &output).ok() && output.empty(), "unloaded decoder");
}

void trusted_function_tests()
{
    const std::array<std::uint8_t, 4> input{1, 2, 3, 4};
    std::vector<std::uint8_t> output{9};
    OodleDecoder decoder;
    const auto before = calls;
    return_count = 4;
    check(!OodleDecoder::from_verified_function(fake_decode, nullptr).ok(), "null factory output");
    check(!OodleDecoder::from_verified_function(nullptr, &decoder).ok() && !decoder.available(),
        "null trusted function accepted");
    check(OodleDecoder::from_verified_function(fake_decode, &decoder).ok() && decoder.available(),
        "trusted function creation");
    auto retained = decoder;
    decoder = {};
    check(retained.available() && retained.decompress(input, 4, &output).ok() &&
        output == std::vector<std::uint8_t>(4, 0x5a) && arguments_valid && calls == before + 1,
        "trusted copy lost callback or ABI");
    decoder = retained;
    check(!OodleDecoder::from_verified_function(nullptr, &decoder).ok() && !decoder.available() &&
        retained.available(), "failed factory retained stale decoder or invalidated copy");
    for (auto count : {std::intptr_t{-1}, std::intptr_t{0}, std::intptr_t{3}, std::intptr_t{5}}) {
        return_count = count;
        output = {9};
        check(!retained.decompress(input, 4, &output).ok() && output.empty(), "trusted return count");
    }
    return_count = 4;
    check(!retained.decompress(std::span(input).first(3), 4, &output).ok() && output.empty(),
        "trusted truncated block");
    const auto rejected_before = calls;
    output = {1, 2, 3, 4};
    check(!retained.decompress(output, 4, &output).ok() && output.empty(), "trusted aliased input");
    check(!retained.decompress(input, kOodleMaximumBlockBytes + 1, &output).ok() && output.empty(),
        "trusted output bound");
    std::vector<std::uint8_t> oversized(kOodleMaximumBlockBytes + 1, 1);
    check(!retained.decompress(oversized, 4, &output).ok() && output.empty(), "trusted input bound");
    check(!retained.decompress(input, 4, nullptr).ok() && calls == rejected_before,
        "trusted rejected argument invoked callback");
    retained = {};
    check(detail::decode_oodle_block(fake_decode, input, 4, &output).ok() && arguments_valid,
        "wrapper destruction affected caller-owned callback");
}

void path_tests()
{
    OodleDecoder decoder;
    check(!OodleDecoder::load({}, nullptr).ok(), "null decoder output");
    for (const auto& path : {std::filesystem::path{}, std::filesystem::path("oo2core_9_win64.dll"),
            std::filesystem::path("C:/missing/../oo2core_9_win64.dll"),
            std::filesystem::path("C:/missing/oo2core_9_win64.dll:stream")}) {
        check(!OodleDecoder::load(path, &decoder).ok() && !decoder.available(), "unsafe path accepted");
    }
#ifdef _WIN32
    std::array<wchar_t, 32768> local{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    check(length > 0 && length < local.size(), "LOCALAPPDATA missing/overlong");
    const auto directory = std::filesystem::path(local.data()) / "Temp/opencode" /
        ("oodle-selftest-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "temporary fixture directory");
    const auto dll = directory / "oo2core_9_win64.dll";
    try {
        { std::ofstream file(dll, std::ios::binary); file << "not an executable and not the pinned companion"; }
        const auto result = OodleDecoder::load(dll, &decoder);
        check(!result.ok() && result.message.find("SHA256 mismatch") != std::string::npos && !decoder.available(),
            "wrong hash did not reject before DLL loading");
        const auto link = directory / "redirect";
        if (CreateSymbolicLinkW(link.c_str(), directory.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2)) {
            const auto redirected = OodleDecoder::load(link / dll.filename(), &decoder);
            std::filesystem::remove(link);
            check(!redirected.ok() && redirected.message.find("plain directories and a regular file") != std::string::npos &&
                !decoder.available(), "reparse ancestor did not reject before wrong-hash leaf");
            std::cout << "reparse ancestor rejected before leaf authentication\n";
        } else {
            std::cout << "reparse fixture unavailable (Windows symlink privilege/developer mode required)\n";
        }
    } catch (...) {
        std::filesystem::remove(dll);
        std::filesystem::remove(directory);
        throw;
    }
    std::filesystem::remove(dll);
    std::filesystem::remove(directory);
#else
    const auto status = OodleDecoder::load("/explicit/oo2core_9_win64.dll", &decoder);
    check(!status.ok() && status.message.find("unsupported") != std::string::npos, "non-Windows support claim");
#endif
}

#ifdef _WIN32
void preexisting_module_test(std::filesystem::path path)
{
    path.make_preferred();
    const auto external = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(external != nullptr, "external companion preload failed");
    try {
        OodleDecoder rejected;
        const auto result = OodleDecoder::load(path, &rejected);
        check(!result.ok() && result.message.find("already loaded") != std::string::npos && !rejected.available(),
            "preexisting full-path image accepted");
        std::vector<std::uint8_t> output{9};
        const std::array<std::uint8_t, 4> input{1, 2, 3, 4};
        check(!rejected.decompress(input, 4, &output).ok() && output.empty(), "rejected image exposed decoder");
        check(GetModuleHandleW(path.c_str()) == external, "rejection changed external module ownership");
        const auto function = reinterpret_cast<detail::OodleDecompressFunction>(
            GetProcAddress(external, "OodleLZ_Decompress"));
        check(function != nullptr, "external callback export missing");
        OodleDecoder borrowed;
        check(OodleDecoder::from_verified_function(function, &borrowed).ok(), "external callback wrapper");
        auto copy = borrowed;
        borrowed = {};
        check(copy.available() && GetModuleHandleW(path.c_str()) == external,
            "callback copy changed external module ownership");
        copy = {};
        check(GetModuleHandleW(path.c_str()) == external,
            "callback wrapper unloaded caller-owned module");
    } catch (...) {
        FreeLibrary(external);
        throw;
    }
    check(FreeLibrary(external) != 0, "external preload cleanup failed");
    check(GetModuleHandleW(path.c_str()) == nullptr, "rejected load retained external module reference");
    std::cout << "preexisting full-path image rejected without acquiring decoder\n";
}
#endif
} // namespace

// Optional local-only evidence: <explicit DLL> [<compressed block> <golden decoded block>].
// No copyrighted block is committed and no source-container extraction runs here.
int main(int argc, char** argv)
{
    try {
        check(argc == 1 || argc == 2 || argc == 4, "usage: oodle_decoder_selftest [DLL [compressed golden]]");
        seam_tests();
        trusted_function_tests();
        path_tests();
        if (argc >= 2) {
#ifdef _WIN32
            preexisting_module_test(std::filesystem::absolute(argv[1]));
#endif
            ff7rp::pipeline::OodleDecoder decoder;
            const auto status = ff7rp::pipeline::OodleDecoder::load(std::filesystem::absolute(argv[1]), &decoder);
            check(status.ok() && decoder.available(), status.message.c_str());
            auto retained = decoder;
            decoder = {};
            check(retained.available(), "copy lost authenticated module lifetime");
            ff7rp::pipeline::OodleDecoder duplicate;
            const auto repeated = ff7rp::pipeline::OodleDecoder::load(std::filesystem::absolute(argv[1]), &duplicate);
            check(!repeated.ok() && repeated.message.find("already loaded") != std::string::npos &&
                !duplicate.available() && retained.available(), "repeated load bypassed copy-only ownership");
            if (argc == 4) {
                const auto compressed = read_block(argv[2]);
                const auto golden = read_block(argv[3]);
                std::vector<std::uint8_t> decoded;
                check(retained.decompress(compressed, golden.size(), &decoded).ok() && decoded == golden,
                    "actual Oodle block differs from golden bytes");
                check(!retained.decompress(std::span(compressed).first(1), golden.size(), &decoded).ok() && decoded.empty(),
                    "actual truncated Oodle block accepted");
                std::cout << "actual pinned companion decoded exact golden block\n";
            }
            check(!ff7rp::pipeline::OodleDecoder::load("relative.dll", &retained).ok() && !retained.available(),
                "failed reload retained stale decoder");
        }
        std::cout << "oodle_decoder_selftest passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
