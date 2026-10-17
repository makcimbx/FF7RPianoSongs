#include "pipeline/native_iostore_source.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace {
using namespace ff7rp::pipeline;
using Bytes = std::vector<std::uint8_t>;
constexpr NativeIoChunkId kId{1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 2};

void put(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void put_be5(Bytes& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 5; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * (4 - i)));
}
Bytes fixture() {
    // Two blocks with unrelated disk padding and one chunk crossing their
    // decoded boundary. Directory bytes are opaque to chunk-ID extraction.
    Bytes toc(144 + 12 + 10 + 24 + 32 + 1 + 33, 0);
    std::memcpy(toc.data(), "-==--==--==--==-", 16);
    toc[16] = 2;
    put(toc, 20, 144, 4);
    put(toc, 24, 1, 4);
    put(toc, 28, 2, 4);
    put(toc, 32, 12, 4);
    put(toc, 36, 1, 4);
    put(toc, 40, 32, 4);
    put(toc, 44, 65536, 4);
    put(toc, 48, 1, 4);
    toc[80] = 9;
    std::copy(kId.begin(), kId.end(), toc.begin() + 144);
    put_be5(toc, 156, 65533);
    put_be5(toc, 161, 8);
    put(toc, 166, 7, 5);
    put(toc, 171, 65536, 3);
    put(toc, 174, 65536, 3);
    put(toc, 178, 65550, 5);
    put(toc, 183, 5, 3);
    put(toc, 186, 5, 3);
    std::memcpy(toc.data() + 190, "oodle", 5);
    return toc;
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
} // namespace

int main(int argc, char** argv) {
    // Optional read-only installed-source check for the two qualified table IDs.
    // The current installation requires Oodle, so fail-closed is the expected
    // result, not successful extraction or provenance validation.
    if (argc != 1 && argc != 2) return fail("Usage: native_iostore_source_selftest [installed-pakchunk3.utoc]");
    if (argc == 2) {
        const NativeIoChunkId ids[] = {
            {0x77, 0x3d, 0x97, 0x22, 0x0e, 0xbd, 0xe2, 0x26, 0, 0, 0, 2},
            {0xed, 0x31, 0x3f, 0xea, 0x77, 0x6e, 0x3a, 0xdd, 0, 0, 0, 2}};
        for (const auto& id : ids) {
            Bytes output{1};
            const auto status = read_native_stored_chunk(argv[1], id, output);
            if (status.ok() || !output.empty() || status.message.find("requires compressed decoding") == std::string::npos)
                return fail("Installed source did not reach the expected compressed decoder boundary");
        }
        std::cout << "Installed table TOCs parsed; compressed decoding correctly rejected\n";
    }
    const auto toc = fixture();
    Bytes cas(65555, 0xee);
    const Bytes expected{1, 2, 3, 4, 5, 6, 7, 8};
    std::copy_n(expected.begin(), 3, cas.begin() + 7 + 65533);
    std::copy(expected.begin() + 3, expected.end(), cas.begin() + 65550);
    Bytes output{0xff};
    auto status = extract_native_stored_chunk(toc, cas, kId, output);
    if (!status.ok()) return fail(status.message.c_str());
    if (output != expected) return fail("Cross-block partial span was not extracted exactly");
    auto alias = cas;
    if (!extract_native_stored_chunk(toc, alias, kId, alias).ok() || alias != expected)
        return fail("UCAS/output aliasing failed");
    alias = toc;
    if (!extract_native_stored_chunk(alias, cas, kId, alias).ok() || alias != expected)
        return fail("TOC/output aliasing failed");
    auto missing = kId;
    missing[0] ^= 1;
    if (extract_native_stored_chunk(toc, cas, missing, output).code != StatusCode::NotFound || !output.empty())
        return fail("Missing chunk did not fail empty");
    // All malformed trust-boundary fields fail without leaving partial bytes.
    const std::size_t corrupt[] = {0, 16, 17, 20, 24, 28, 32, 36, 40, 44, 48,
        52, 64, 80, 81, 143, 156, 161, 170, 171, 174, 177, 182, 183, 186, 189, 221};
    for (const auto offset : corrupt) {
        auto bad = toc;
        bad[offset] = 0xff;
        output = {42};
        if (extract_native_stored_chunk(bad, cas, kId, output).ok() || !output.empty())
            return fail("Malformed TOC field accepted or partial output leaked");
    }
    for (std::size_t size = 0; size < toc.size(); ++size) {
        const Bytes bad(toc.begin(), toc.begin() + size);
        if (extract_native_stored_chunk(bad, cas, kId, output).ok() || !output.empty())
            return fail("Truncated TOC accepted");
    }
    auto short_cas = cas;
    short_cas.pop_back();
    if (extract_native_stored_chunk(toc, short_cas, kId, output).ok() || !output.empty())
        return fail("Truncated disk block accepted");
    auto duplicate = toc;
    // Insert a second ID and offset record while keeping each table well-sized.
    duplicate.insert(duplicate.begin() + 156, kId.begin(), kId.end());
    duplicate.insert(duplicate.begin() + 178, 10, 0);
    duplicate.insert(duplicate.end(), 33, 0);
    put(duplicate, 24, 2, 4);
    if (extract_native_stored_chunk(duplicate, cas, kId, output).ok() || !output.empty())
        return fail("Ambiguous duplicate chunk ID accepted");
    auto compressed = toc;
    compressed[177] = 1;
    status = extract_native_stored_chunk(compressed, cas, kId, output);
    if (status.ok() || !output.empty() || status.message.find("requires compressed decoding") == std::string::npos)
        return fail("Compressed chunk did not fail at the explicit decoder boundary");
    std::cout << "native_iostore_source_selftest passed\n";
    return 0;
}
