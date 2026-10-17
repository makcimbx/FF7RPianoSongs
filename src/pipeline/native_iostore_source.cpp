#include "native_iostore_source.h"
#include "oodle_decoder.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>

namespace ff7rp::pipeline {
namespace {

// Format evidence: trumank/retoc 234cd0d86e5d2db21d187b8133efea456737802e,
// main.rs FIoStoreTocHeader, FIoOffsetAndLength, compressed block entries.
constexpr std::size_t kLimit = 64 * 1024 * 1024;
constexpr char kMagic[] = "-==--==--==--==-";

std::uint64_t le(const std::uint8_t* p, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value |= std::uint64_t{p[i]} << (i * 8);
    return value;
}

std::uint64_t be5(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 5; ++i) value = (value << 8) | p[i];
    return value;
}

Status invalid(const char* message) {
    return Status::error(StatusCode::InvalidArgument, message);
}

struct Slice {
    std::uint64_t disk_offset;
    std::size_t disk_bytes;
    std::size_t skip;
    std::size_t take;
    std::size_t decoded_bytes;
    bool oodle;
};

Status plan(const std::vector<std::uint8_t>& toc, std::uint64_t cas_bytes,
    const NativeIoChunkId& chunk, std::vector<Slice>& slices, const OodleDecoder* decoder) {
    if (toc.size() < 144 || toc.size() > kLimit || std::memcmp(toc.data(), kMagic, 16) != 0 ||
        toc[16] != 2 || le(toc.data() + 20, 4) != 144 || le(toc.data() + 32, 4) != 12) {
        return invalid("Unsupported or truncated native source TOC (requires UE4 DirectoryIndex version)");
    }
    // Version 2 predates partitioning/perfect hashes. Reject reserved fields,
    // unknown flags, encryption and signatures rather than skipping semantics.
    if (le(toc.data() + 17, 3) != 0 || le(toc.data() + 52, 4) != 0 ||
        !std::all_of(toc.begin() + 64, toc.begin() + 80, [](auto b) { return b == 0; }) ||
        (toc[80] & ~std::uint8_t{9}) != 0 ||
        !std::all_of(toc.begin() + 81, toc.begin() + 144, [](auto b) { return b == 0; })) {
        return invalid("Unsupported encrypted/signed/partitioned native source TOC");
    }
    const auto entries = le(toc.data() + 24, 4);
    const auto blocks = le(toc.data() + 28, 4);
    const auto methods = le(toc.data() + 36, 4);
    const auto method_bytes = le(toc.data() + 40, 4);
    const auto block_size = le(toc.data() + 44, 4);
    const auto directory_bytes = le(toc.data() + 48, 4);
    if (!entries || entries > 65536 || !blocks || blocks > 1048576 ||
        methods > 16 || (methods && method_bytes != 32) ||
        block_size != 65536 || !(toc[80] & 8) || !directory_bytes) {
        return invalid("Unsupported native source TOC table or block geometry");
    }
    // Widen all file-controlled arithmetic before multiplication/addition.
    const auto offsets_start = 144 + entries * 12;
    const auto blocks_start = offsets_start + entries * 10;
    const auto methods_start = blocks_start + blocks * 12;
    const auto directory_start = methods_start + methods * method_bytes;
    const auto expected_end = directory_start + directory_bytes + entries * 33;
    if (expected_end != toc.size()) return invalid("Native source TOC table extents do not match file");
    for (std::uint64_t i = 0; i < methods; ++i) {
        const auto* name = toc.data() + methods_start + i * method_bytes;
        const auto* end = std::find(name, name + method_bytes, 0);
        if (end == name || end == name + method_bytes ||
            !std::all_of(end, name + method_bytes, [](auto b) { return b == 0; }))
            return invalid("Malformed native source compression method name");
    }
    std::size_t selected = std::numeric_limits<std::size_t>::max();
    std::set<NativeIoChunkId> ids;
    for (std::uint64_t i = 0; i < entries; ++i) {
        NativeIoChunkId id;
        std::copy_n(toc.begin() + 144 + static_cast<std::size_t>(i) * 12, 12, id.begin());
        if (!ids.insert(id).second) return invalid("Ambiguous duplicate native source chunk IDs");
        if (id == chunk) selected = static_cast<std::size_t>(i);
    }
    if (selected == std::numeric_limits<std::size_t>::max())
        return Status::error(StatusCode::NotFound, "Native source chunk is absent");
    const auto* extent = toc.data() + offsets_start + selected * 10;
    const auto offset = be5(extent);
    const auto length = be5(extent + 5);
    if (!length || length > kLimit || offset / block_size >= blocks ||
        (offset + length - 1) / block_size >= blocks)
        return invalid("Native source chunk span is empty, oversized or outside block table");
    const auto first = offset / block_size;
    const auto last = (offset + length - 1) / block_size;
    std::uint64_t remaining = length;
    for (auto i = first; i <= last; ++i) {
        const auto* block = toc.data() + blocks_start + i * 12;
        const auto disk_offset = le(block, 5);
        const auto compressed = le(block + 5, 3);
        const auto decoded = le(block + 8, 3);
        const auto method = block[11];
        if (!compressed || compressed>block_size || !decoded || decoded > block_size || method > methods ||
            disk_offset > cas_bytes || compressed > cas_bytes - disk_offset)
            return invalid("Native source block has invalid size, method or disk extent");
        if (method != 0) {
            const auto* name=toc.data()+methods_start+(method-1)*method_bytes;
            if(std::strcmp(reinterpret_cast<const char*>(name),"oodle")!=0)
                return invalid("Unsupported native source compression method");
            if(!decoder||!decoder->available()) return invalid("Native source chunk requires compressed decoding; no installed-game decoder is available");
        } else if (compressed != decoded) return invalid("Stored native source block sizes differ");
        const auto skip = i == first ? offset % block_size : 0;
        const auto take = std::min(remaining, block_size - skip);
        if (skip > decoded || take > decoded - skip)
            return invalid("Native source chunk crosses missing decoded block bytes");
        slices.push_back({disk_offset, static_cast<std::size_t>(compressed),
            static_cast<std::size_t>(skip), static_cast<std::size_t>(take),static_cast<std::size_t>(decoded),method!=0});
        remaining -= take;
    }
    return Status::ok_status();
}

Status io_error(const char* message) {
    return Status::error(StatusCode::IoError, message);
}

} // namespace

Status extract_native_chunk(const std::vector<std::uint8_t>& toc,
    const std::vector<std::uint8_t>& cas, const NativeIoChunkId& chunk,
    std::vector<std::uint8_t>& output, const OodleDecoder* decoder) {
    // Build separately: callers may alias output with toc or cas.
    std::vector<Slice> slices;
    const auto status = plan(toc, cas.size(), chunk, slices,decoder);
    if (!status.ok()) { output.clear(); return status; }
    std::vector<std::uint8_t> result;
    for (const auto& slice : slices) {
        if(slice.oodle) {
            std::vector<std::uint8_t> decoded;
            auto s=decoder->decompress(std::span(cas.data()+slice.disk_offset,slice.disk_bytes),slice.decoded_bytes,&decoded);
            if(!s.ok()){output.clear();return s;}
            if(decoded.size()!=slice.decoded_bytes){output.clear();return invalid("Oodle decoded size mismatch");}
            result.insert(result.end(),decoded.begin()+slice.skip,decoded.begin()+slice.skip+slice.take); continue;
        }
        const auto start = cas.begin() + static_cast<std::size_t>(slice.disk_offset) + slice.skip;
        result.insert(result.end(), start, start + slice.take);
    }
    output = std::move(result);
    return Status::ok_status();
}

Status read_native_chunk(const std::filesystem::path& toc_path,
    const NativeIoChunkId& chunk, std::vector<std::uint8_t>& output, const OodleDecoder* decoder) {
    output.clear();
    std::ifstream toc_file(toc_path, std::ios::binary | std::ios::ate);
    if (!toc_file) return io_error("Cannot open native source TOC");
    const auto toc_size = toc_file.tellg();
    if (toc_size < 144 || toc_size > static_cast<std::streamoff>(kLimit))
        return invalid("Native source TOC exceeds bounded size");
    std::vector<std::uint8_t> toc(static_cast<std::size_t>(toc_size));
    toc_file.seekg(0);
    if (!toc_file.read(reinterpret_cast<char*>(toc.data()), static_cast<std::streamsize>(toc.size())))
        return io_error("Cannot read native source TOC");
    auto cas_path = toc_path;
    cas_path.replace_extension(".ucas");
    std::ifstream cas_file(cas_path, std::ios::binary | std::ios::ate);
    if (!cas_file) return io_error("Cannot open native source UCAS");
    const auto cas_size = cas_file.tellg();
    if (cas_size < 0) return io_error("Cannot determine native source UCAS extent");
    std::vector<Slice> slices;
    const auto status = plan(toc, static_cast<std::uint64_t>(cas_size), chunk, slices,decoder);
    if (!status.ok()) return status;
    std::vector<std::uint8_t> result;
    for (const auto& slice : slices) {
        std::vector<std::uint8_t> block(slice.disk_bytes);
        cas_file.seekg(static_cast<std::streamoff>(slice.disk_offset));
        if (!cas_file.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size())))
            return io_error("Cannot read native source UCAS block");
        if(slice.oodle) {
            std::vector<std::uint8_t> decoded; auto s=decoder->decompress(block,slice.decoded_bytes,&decoded);
            if(!s.ok()) return s;
            if(decoded.size()!=slice.decoded_bytes) return invalid("Oodle decoded size mismatch");
            block=std::move(decoded);
        }
        result.insert(result.end(), block.begin() + slice.skip, block.begin() + slice.skip + slice.take);
    }
    output = std::move(result);
    return Status::ok_status();
}

Status extract_native_stored_chunk(const std::vector<std::uint8_t>& toc,
    const std::vector<std::uint8_t>& cas,const NativeIoChunkId& chunk,std::vector<std::uint8_t>& output) {
    return extract_native_chunk(toc,cas,chunk,output);
}
Status read_native_stored_chunk(const std::filesystem::path& path,
    const NativeIoChunkId& chunk,std::vector<std::uint8_t>& output) {
    return read_native_chunk(path,chunk,output);
}

} // namespace ff7rp::pipeline
