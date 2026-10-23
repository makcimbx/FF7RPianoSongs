#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

namespace {
enum class Failure { None, Open, Create, Data, Finish };
struct Calls {
    Failure failure = Failure::None;
    unsigned open = 0, create = 0, data = 0, finish = 0, destroy = 0, close = 0;
    unsigned algorithms = 0, hashes = 0;
};
thread_local Calls calls;
constexpr NTSTATUS failed = static_cast<NTSTATUS>(0xc0000001u);
NTSTATUS WINAPI test_open(BCRYPT_ALG_HANDLE* out, LPCWSTR id, LPCWSTR provider, ULONG flags) {
    ++calls.open;
    if (calls.failure == Failure::Open) return failed;
    const auto status = BCryptOpenAlgorithmProvider(out, id, provider, flags);
    if (status >= 0) ++calls.algorithms;
    return status;
}
NTSTATUS WINAPI test_create(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* out,
    PUCHAR object, ULONG object_size, PUCHAR secret, ULONG secret_size, ULONG flags) {
    ++calls.create;
    if (calls.failure == Failure::Create) return failed;
    const auto status = BCryptCreateHash(algorithm, out, object, object_size, secret, secret_size, flags);
    if (status >= 0) ++calls.hashes;
    return status;
}
NTSTATUS WINAPI test_data(BCRYPT_HASH_HANDLE hash, PUCHAR bytes, ULONG count, ULONG flags) {
    ++calls.data;
    require(count > 0 && count <= 1024 * 1024, "CNG chunk length");
    if (calls.failure == Failure::Data && calls.data == 2) return failed;
    return BCryptHashData(hash, bytes, count, flags);
}
NTSTATUS WINAPI test_finish(BCRYPT_HASH_HANDLE hash, PUCHAR out, ULONG count, ULONG flags) {
    ++calls.finish;
    if (calls.failure == Failure::Finish) {
        std::fill_n(out, count, 0xa5); // A failed provider must not publish even written output.
        return failed;
    }
    return BCryptFinishHash(hash, out, count, flags);
}
NTSTATUS WINAPI test_destroy(BCRYPT_HASH_HANDLE hash) {
    ++calls.destroy;
    require(calls.hashes == 1, "Hash handle ownership");
    --calls.hashes;
    return BCryptDestroyHash(hash);
}
NTSTATUS WINAPI test_close(BCRYPT_ALG_HANDLE algorithm, ULONG flags) {
    ++calls.close;
    require(calls.algorithms == 1 && calls.hashes == 0, "Provider cleanup order/ownership");
    --calls.algorithms;
    return BCryptCloseAlgorithmProvider(algorithm, flags);
}
void cleaned() {
    require(calls.algorithms == 0 && calls.hashes == 0, "Leaked CNG handle");
    require(calls.close == (calls.failure == Failure::Open ? 0u : 1u), "Provider cleanup count");
    require(calls.destroy == (calls.failure == Failure::Open || calls.failure == Failure::Create ? 0u : 1u),
        "Hash cleanup count");
}
}
#define BCryptOpenAlgorithmProvider test_open
#define BCryptCreateHash test_create
#define BCryptHashData test_data
#define BCryptFinishHash test_finish
#define BCryptDestroyHash test_destroy
#define BCryptCloseAlgorithmProvider test_close
#endif

// Standalone translation unit: internal helpers and fault injection need no production API.
#include "../pipeline/native_asset_hash.cpp"

#ifdef _WIN32
#undef BCryptOpenAlgorithmProvider
#undef BCryptCreateHash
#undef BCryptHashData
#undef BCryptFinishHash
#undef BCryptDestroyHash
#undef BCryptCloseAlgorithmProvider
#endif

using namespace ff7rp::pipeline;
namespace {
using Digest = std::array<std::uint8_t,32>;
std::string hex(const Digest& digest) {
    std::string result;
    for (auto byte : digest) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}
void equivalent(std::span<const std::uint8_t> bytes) {
    const auto expected = portable_sha256(bytes);
#ifdef _WIN32
    calls = {};
    Digest actual{};
    require(platform_sha256(bytes, actual), "CNG SHA256 failed");
    require(actual == expected, "CNG/portable digest mismatch");
    require(calls.open == 1 && calls.create == 1 && calls.finish == 1, "CNG success path");
    require(calls.data == (bytes.size() + 1024 * 1024 - 1) / (1024 * 1024), "CNG chunk count");
    cleaned();
    calls = {};
#endif
    require(native_sha256(bytes) == expected, "Public SHA256 digest mismatch");
#ifdef _WIN32
    cleaned();
#endif
}
void known(std::string_view text, const char* expected) {
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    require(hex(portable_sha256(bytes)) == expected, "Known SHA256 vector mismatch");
    equivalent(bytes);
}
}

int main() {
    known("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    known("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    known("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    known(std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    equivalent({}); // Null empty span is valid too.
    std::vector<std::uint8_t> bytes(2 * 1024 * 1024 + 17);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 131 + i / 251) & 255);
    for (std::size_t size = 0; size <= 130; ++size) equivalent(std::span(bytes).first(size));
    for (const std::size_t size : {1024u * 1024 - 1, 1024u * 1024, 1024u * 1024 + 1,
        2u * 1024 * 1024, 2u * 1024 * 1024 + 17}) equivalent(std::span(bytes).first(size));
#ifdef _WIN32
    const auto expected = portable_sha256(bytes);
    for (auto failure : {Failure::Open, Failure::Create, Failure::Data, Failure::Finish}) {
        calls = {}; calls.failure = failure;
        Digest untouched; untouched.fill(0x5a);
        auto actual = untouched;
        require(!platform_sha256(bytes, actual) && actual == untouched, "Failure published partial digest");
        cleaned();
        calls = {}; calls.failure = failure;
        require(native_sha256(bytes) == expected, "CNG failure did not fall back exactly");
        cleaned();
    }
#endif
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        const auto input = std::span(bytes).subspan(worker, 1024 * 1024 + 1);
        const auto expected = portable_sha256(input);
        for (unsigned round = 0; round < 5; ++round) {
#ifdef _WIN32
            calls = {}; calls.failure = static_cast<Failure>(round);
#endif
            require(native_sha256(input) == expected, "Concurrent SHA256 mismatch");
#ifdef _WIN32
            cleaned();
#endif
        }
    });
    for (auto& worker : workers) worker.join();
#ifdef _WIN32
    calls = {};
#endif
    const auto start = std::chrono::steady_clock::now();
    const auto scalar = portable_sha256(bytes);
    const auto middle = std::chrono::steady_clock::now();
    const auto platform = native_sha256(bytes);
    const auto end = std::chrono::steady_clock::now();
    require(platform == scalar, "Benchmark digest mismatch");
    std::cout << "Informational SHA256 (" << bytes.size() << " bytes), scalar_us="
        << std::chrono::duration_cast<std::chrono::microseconds>(middle - start).count()
        << " public_us=" << std::chrono::duration_cast<std::chrono::microseconds>(end - middle).count()
        << "\nnative_asset_hash_selftest passed\n";
}
