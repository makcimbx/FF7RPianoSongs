#include "pipeline/native_music_export.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/native_binary.h"
#include <bit>
#include <limits>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace ff7rp::pipeline;
constexpr std::size_t kBankOffset = 144;
constexpr std::size_t kBankBytes = 11227936;

std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i] << 8);
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0));
    }
    return crc;
}

void checksum(std::uint8_t* data, std::size_t size) {
    const auto crc = crc16(data, size - 2);
    data[size - 2] = static_cast<std::uint8_t>(crc >> 8);
    data[size - 1] = static_cast<std::uint8_t>(crc);
}

// Entirely synthetic CRC-valid HCA data, not decodable audio or copied assets.
Bytes bank(std::uint8_t fill, std::uint16_t appended = 1013, std::size_t frames=5487) {
    constexpr std::size_t block = 682;
    Bytes hca(96 + frames * block, 0);
    std::memcpy(hca.data(), "HCA\0", 4);
    hca[4] = 2;
    hca[7] = 96;
    std::memcpy(hca.data() + 8, "fmt\0", 4);
    const std::uint8_t geometry[] = {2, 0, 0xbb, 0x80, 0, 0, 0x15, 0x6f, 0, 0x80};
    std::copy(std::begin(geometry), std::end(geometry), hca.begin() + 12);
    for(int i=0;i<4;++i) hca[16+i]=std::uint8_t(frames>>(24-8*i));
    hca[22] = static_cast<std::uint8_t>(appended >> 8);
    hca[23] = static_cast<std::uint8_t>(appended);
    std::memcpy(hca.data() + 24, "comp", 4);
    hca[28] = 2;
    hca[29] = 0xaa;
    checksum(hca.data(), 96);
    std::array<std::uint8_t, block> frame;
    frame.fill(fill);
    frame[0] = 0xff;
    frame[1] = 0xff;
    checksum(frame.data(), frame.size());
    for (std::size_t i = 0; i < frames; ++i)
        std::copy(frame.begin(), frame.end(), hca.begin() + 96 + i * block);
    const auto result = build_mabf_from_mode_hca({&hca, &hca, &hca});
    return result.status.ok() ? result.bytes : Bytes{};
}

Bytes source_fixture(const Bytes& mabf) {
    // Qualified serialized format fields only; no genuine Music/audio payload.
    constexpr char hex[] =
        "00040d0201061302100300000000010000000400000000000000000000000000"
        "0000030000000f00000000000000000000001000000000000000010000001100"
        "00000000000002000000000000000100000003000000000000008007056210ea"
        "42010101cdcc4c3f6210ea42000000003053ab00000001001000040000000000"
        "21000000000000002053ab002053ab00";
    static_assert(sizeof(hex) - 1 == kBankOffset * 2);
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    Bytes result(kBankOffset);
    for (std::size_t i = 0; i < kBankOffset; ++i)
        result[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    result.insert(result.end(), mabf.begin(), mabf.end());
    return result;
}

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}
} // namespace

int main(int argc, char** argv) {
    // Optional read-only compatibility check: an export body or legacy .uexp
    // and a pipeline MABF. Genuine bytes stay external and are never written.
    if (argc != 1 && argc != 3) return fail("Usage: native_music_export_selftest [export-body mabf]");
    if (argc == 3) {
        std::ifstream source_file(argv[1], std::ios::binary), bank_file(argv[2], std::ios::binary);
        if (!source_file || !bank_file) return fail("Cannot read external compatibility inputs");
        Bytes external_source{std::istreambuf_iterator<char>(source_file), {}};
        const Bytes external_bank{std::istreambuf_iterator<char>(bank_file), {}};
        constexpr std::uint8_t legacy_tag[] = {0xc1, 0x83, 0x2a, 0x9e};
        if (external_source.size() >= 4 && std::equal(std::begin(legacy_tag), std::end(legacy_tag),
                external_source.end() - 4))
            external_source.resize(external_source.size() - 4);
        const auto external = build_native_music_export(external_source, external_bank);
        if (!external.status.ok()) return fail(external.status.message.c_str());
        if (!verify_native_music_export(external_source, external_bank, external.bytes).ok())
            return fail("External export compatibility verification failed");
        std::cout << "External fixed-geometry export compatibility passed\n";
    }
    const auto original_bank = bank(0x11);
    const auto replacement = bank(0x77);
    const auto source = source_fixture(original_bank);
    const auto fixture_gain = std::bit_cast<float>(std::uint32_t(native_binary::get(source, 0x64))) *
        std::bit_cast<float>(std::uint32_t(native_binary::get(original_bank, 0x98)));
    if (native_music_fixed_gain() != fixture_gain ||
        native_music_fixed_gain() != 0.8f * 0.535f)
        return fail("Preview fixed gain differs from retained native asset/bank gains");
    const auto original_source = source;
    const auto original_replacement = replacement;
    MabfArtifactMetadata source_timing;
    if(!validate_structural_mabf(original_bank,&source_timing).ok()) return fail("Source timing fixture");
    const NativeMusicCueTiming qualified_timing{source_timing.logical_source_frames,source_timing.sample_rate,
        double(source_timing.logical_source_frames)/source_timing.sample_rate};
    const auto old_profile=NativeMusicExportProfile::Older1004;
    auto good_old=[&](const std::vector<std::uint8_t>& input,const std::vector<std::uint8_t>& bank_bytes,const NativeMusicCueTiming& timing,std::size_t volume) {
        using namespace native_binary;
        auto b=build_native_music_export(input,bank_bytes,timing,old_profile);
        const auto n=bank_bytes.size()-48,tail=144+get(input,0x88);
        if(!b.status.ok()||b.bytes.size()!=160+n+40||!verify_native_music_export(input,bank_bytes,timing,old_profile,b.bytes).ok()) return false;
        std::vector<unsigned> selected; unsigned index=0;
        for(std::size_t at=0;at<16;at+=2) {
            auto fragment=get(b.bytes,at,2); if(fragment&0x80) return false; index+=unsigned(fragment&127);
            for(unsigned j=0;j<(fragment>>9);++j) selected.push_back(index++);
            if(bool(fragment&0x100)!=(at==14)) return false;
        }
        if(selected!=std::vector<unsigned>{0,1,15,17,18,25,27,38,40,55}) return false;
        for(std::size_t i=0x0a;i<0x61;++i) if(i<0x5d&&b.bytes[i+6]!=input[i]) return false;
        const auto seconds=std::bit_cast<std::uint32_t>(float(double(timing.logical_source_frames)/timing.sample_rate));
        if(get(b.bytes,0x63)!=seconds||get(b.bytes,0x73)!=seconds||b.bytes[0x67]!=input[0x61]||b.bytes[0x68]!=input[0x62]||
           b.bytes[0x69]!=0||b.bytes[0x6a]!=4||get(b.bytes,0x6b)!=get(input,volume)||get(b.bytes,0x6f)||get(b.bytes,0x77)||
           get(b.bytes,0x7b)!=n+16||get(b.bytes,0x7f)!=0x10000||get(b.bytes,0x83,2)!=16||get(b.bytes,0x85,2)!=9||
           get(b.bytes,0x90,8)!=0x21||get(b.bytes,0x98)!=n||get(b.bytes,0x9c)!=n) return false;
        for(std::size_t i=0x87;i<0x90;++i) if(b.bytes[i]) return false;
        return std::equal(bank_bytes.begin(),bank_bytes.end()-48,b.bytes.begin()+160)&&
            std::equal(input.begin()+tail,input.begin()+tail+40,b.bytes.end()-40);
    };
    if(!good_old(source,replacement,qualified_timing,0x64)) return fail("Qualified controlled older donor projection");
    if(!good_old(source,original_bank,qualified_timing,0x64)) return fail("Older no-op source bank preservation");
    const auto old_output=build_native_music_export(source,replacement,qualified_timing,old_profile);
    for(auto at:{0x58,0x63,0x69,0x6a,0x6f,0x73,0x7b,0x85,0x98}) {
        auto forged=old_output.bytes;forged[at]^=1;
        if(verify_native_music_export(source,replacement,qualified_timing,old_profile,forged).ok()) return fail("Forged controlled older field accepted");
    }
    auto old_source=source; old_source[5]=4; old_source.erase(old_source.begin()+0x63); old_source.insert(old_source.begin()+0x7f,0);
    native_binary::put(old_source,0x79,5,2);
    if(!good_old(old_source,replacement,qualified_timing,0x63)) return fail("Explicit older reflected/header fixture");
    auto old40=old_source;old40.resize(old40.size()-8);old40[old40.size()-20]^=0x40;
    if(!good_old(old40,replacement,qualified_timing,0x63)) return fail("Older suffix40 source GUID preservation");
    auto compact_old=old_source;
    for(auto at:{0x26,0x32,0x3e}) native_binary::put(compact_old,at,native_binary::get(compact_old,at)-1);
    if(!good_old(compact_old,replacement,qualified_timing,0x63)) return fail("Compact older mode NameMap indexes");
    for(auto at:{0x5,0x5a,0x5c,0x61,0x62,0x63,0x6b,0x6f,0x73,0x77,0x79,0x7b,0x80,0x88,0x8c}) {
        auto invalid=old_source; invalid[at]^=1;
        auto b=build_native_music_export(invalid,replacement,qualified_timing,old_profile);
        if(b.status.ok()||!b.bytes.empty()) return fail("Malformed older profile bypassed validation");
    }
    auto mismatched_old=old_source; mismatched_old[0x67]^=1;
    if(build_native_music_export(mismatched_old,replacement,qualified_timing,old_profile).status.ok()) return fail("Older nested/inherited duration disagreement accepted");
    if(verify_native_music_export(old_source,replacement,qualified_timing,old_profile,old_source).ok()) return fail("Unqualified older candidate verified");
    const auto retimed_noop=build_native_music_export(source,original_bank,qualified_timing);
    if(!retimed_noop.status.ok()||!std::equal(original_bank.begin(),original_bank.end(),retimed_noop.bytes.begin()+144)||
       !verify_native_music_export(source,original_bank,qualified_timing,retimed_noop.bytes).ok()) return fail("Variable no-op bank preservation");
    // Variable extent, including structurally valid modes with unequal padded
    // frame counts but identical exact logical source duration.
    for(auto frames : {std::size_t(8),std::size_t(6000)}) {
        using namespace ff7rp::pipeline::native_binary;
        std::array<Bytes,3> modes{bank(0x22,1013,frames),bank(0x33,2037,frames+1),bank(0x44,3061,frames+2)};
        Bytes mixed(modes[0].begin(),modes[0].begin()+0x460);
        std::array<std::size_t,3> starts{};
        for(std::size_t i=0;i<3;++i) {
            starts[i]=mixed.size();
            auto begin=i?0x440+get(modes[i],i==1?0x424:0x428,4):0x460;
            auto end=i==2?modes[i].size():0x440+get(modes[i],i==0?0x424:0x428,4);
            mixed.insert(mixed.end(),modes[i].begin()+begin,modes[i].begin()+end);
        }
        put(mixed,12,mixed.size()-48); put(mixed,0x424,starts[1]-0x440); put(mixed,0x428,starts[2]-0x440);
        put(mixed,starts[1]-46+0x16,(frames+1)*682); put(mixed,starts[2]-46+0x16,(frames+2)*682);
        MabfArtifactMetadata meta; auto ms=validate_structural_mabf(mixed,&meta); if(!ms.ok()) return fail(ms.message.c_str());
        NativeMusicCueTiming timing{meta.logical_source_frames,meta.sample_rate,double(meta.logical_source_frames)/meta.sample_rate};
        if(!good_old(source,mixed,timing,0x64)||!good_old(old40,mixed,timing,0x63)) return fail("Older variable/unequal mode-slot projection");
        const auto variable=build_native_music_export(source,mixed,timing);
        if(!variable.status.ok()) return fail(variable.status.message.c_str());
        if(variable.bytes.size()!=144+mixed.size()||get(variable.bytes,0x70,4)!=mixed.size()-48+16 ||
           get(variable.bytes,0x88,4)!=mixed.size()-48||get(variable.bytes,0x8c,4)!=mixed.size()-48 ||
           get(variable.bytes,0x5d,4)!=std::bit_cast<std::uint32_t>(float(timing.duration_seconds))||
           get(variable.bytes,0x68,4)!=get(variable.bytes,0x5d,4)||
           !std::equal(source.end()-48,source.end(),variable.bytes.end()-48)||
           !verify_native_music_export(source,mixed,timing,variable.bytes).ok()) return fail("Variable Music image/timing projection");
        for(std::size_t i=0;i<144;++i) if(!((i>=0x5d&&i<0x61)||(i>=0x68&&i<0x6c)||(i>=0x70&&i<0x74)||(i>=0x88&&i<0x90))&&source[i]!=variable.bytes[i]) return fail("Opaque Music prefix changed");
        for(auto seconds : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::max(),timing.duration_seconds+1}) {
            auto bad_timing=timing; bad_timing.duration_seconds=seconds;
            if(build_native_music_export(source,mixed,bad_timing).status.ok()) return fail("Invalid cue float accepted");
        }
        auto bad_timing=timing; bad_timing.sample_rate=0;
        if(build_native_music_export(source,mixed,bad_timing).status.ok()) return fail("Zero cue rate");
        bad_timing=timing; bad_timing.sample_rate=44100;
        if(build_native_music_export(source,mixed,bad_timing).status.ok()) return fail("Mismatching cue rate");
        bad_timing=timing; bad_timing.logical_source_frames=std::numeric_limits<std::uint64_t>::max();
        if(build_native_music_export(source,mixed,bad_timing).status.ok()) return fail("Overflowing cue frame count");
        bad_timing=timing; bad_timing.logical_source_frames=0;
        if(build_native_music_export(source,mixed,bad_timing).status.ok()) return fail("Zero cue frame count");
        auto corrupt=variable.bytes; corrupt[0x70]^=1;
        if(verify_native_music_export(source,mixed,timing,corrupt).ok()) return fail("Invalid relocated image accepted");
    }
    if (original_bank.size() != kBankBytes + 48 || replacement.size() != kBankBytes + 48)
        return fail("Synthetic fixed-geometry bank construction failed");
    auto built = build_native_music_export(source, replacement);
    if (!built.status.ok()) return fail(built.status.message.c_str());
    if (built.bytes.size() != source.size() || built.bytes == source ||
        !std::equal(source.begin(), source.begin() + kBankOffset, built.bytes.begin()) ||
        !std::equal(source.end() - 48, source.end(), built.bytes.end() - 48) ||
        !std::equal(replacement.begin(), replacement.begin() + kBankBytes, built.bytes.begin() + kBankOffset) ||
        !verify_native_music_export(source, replacement, built.bytes).ok())
        return fail("Exact bank replacement/prefix/suffix preservation failed");
    if (source != original_source || replacement != original_replacement)
        return fail("Builder modified its inputs");
    if (build_native_music_export(source, replacement).bytes != built.bytes)
        return fail("Export production is not deterministic");

    // No-op and reference aliasing are safe; unchanged inputs are not corrupt.
    const auto unchanged = build_native_music_export(source, original_bank);
    if (!unchanged.status.ok() || unchanged.bytes != source ||
        !verify_native_music_export(source, original_bank, source).ok() ||
        build_native_music_export(source, source).status.ok() ||
        build_native_music_export(replacement, replacement).status.ok())
        return fail("Unchanged or aliased input handling failed");
    auto assigned = source;
    assigned = build_native_music_export(assigned, replacement).bytes;
    if (assigned != built.bytes) return fail("Input/output assignment aliasing failed");

    // Every serialized wrapper byte is qualified, not just a MABF marker search.
    auto bad = source;
    for (std::size_t i = 0; i < kBankOffset; ++i) {
        bad[i] ^= 0xff;
        const auto rejected = build_native_music_export(bad, replacement);
        if (rejected.status.ok() || !rejected.bytes.empty() ||
            verify_native_music_export(bad, replacement, built.bytes).ok() ||
            build_native_music_export(bad,replacement,qualified_timing).status.ok())
            return fail("Unqualified prefix/count/pointer/padding accepted");
        bad[i] ^= 0xff;
    }
    // Truncation, an appended package tag, and an arbitrary marker are not exports.
    for (const std::size_t size : {std::size_t{0}, std::size_t{143}, source.size() - 1, source.size() + 4}) {
        bad = source;
        bad.resize(size);
        if (build_native_music_export(bad, replacement).status.ok())
            return fail("Truncated or oversized source accepted");
    }
    bad = source;
    std::copy_n("mabf", 4, bad.begin());
    if (build_native_music_export(bad, replacement).status.ok())
        return fail("Arbitrary/ambiguous bank marker accepted");

    for (const auto offset : {kBankOffset, kBankOffset + 12, kBankOffset + 0x424,
            kBankOffset + 0x460 + 94, kBankOffset + 0x460 + 100, source.size() - 1}) {
        bad = source;
        bad[offset] ^= 1;
        if (build_native_music_export(bad, replacement).status.ok() ||
            verify_native_music_export(bad, replacement, built.bytes).ok())
            return fail("Corrupt original bank or suffix accepted");
    }
    for (const std::size_t size : {std::size_t{0}, replacement.size() - 48,
            replacement.size() - 1, replacement.size() + 1}) {
        bad = replacement;
        bad.resize(size);
        if (build_native_music_export(source, bad).status.ok())
            return fail("Unequal replacement geometry accepted");
    }
    for (const auto offset : {std::size_t{0}, std::size_t{12}, std::size_t{0x424},
            std::size_t{0x460 + 94}, std::size_t{0x460 + 100}, replacement.size() - 1}) {
        bad = replacement;
        bad[offset] ^= 1;
        if (build_native_music_export(source, bad).status.ok() ||
            verify_native_music_export(source, bad, built.bytes).ok())
            return fail("Corrupt replacement bank accepted");
    }
    const auto other_duration = bank(0x33, 1012);
    MabfArtifactMetadata metadata;
    if (!validate_structural_mabf(other_duration, &metadata).ok() ||
        build_native_music_export(source, other_duration).status.ok() ||
        build_native_music_export(source_fixture(other_duration), replacement).status.ok())
        return fail("CRC-valid equal-size but unqualified source timing accepted");
    for (const auto offset : {std::size_t{0}, kBankOffset, built.bytes.size() - 1}) {
        bad = built.bytes;
        bad[offset] ^= 1;
        if (verify_native_music_export(source, replacement, bad).ok())
            return fail("Verifier accepted altered candidate prefix/bank/suffix");
    }
    bad = built.bytes;
    bad.pop_back();
    if (verify_native_music_export(source, replacement, bad).ok())
        return fail("Verifier accepted truncated candidate");
    std::cout << "native_music_export_selftest passed (synthetic export-only fixtures)\n";
    return 0;
}
