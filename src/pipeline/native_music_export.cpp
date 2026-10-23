#include "native_music_export.h"

#include "mabf_builder.h"
#include "mabf_scaffold.h"
#include "native_binary.h"

#include <algorithm>
#include <cstring>
#include <bit>
#include <cmath>
#include <limits>

namespace ff7rp::pipeline {
namespace {

// Qualified serialized property/wrapper bytes, not a generic Music grammar.
// verify_preserved_music.py: reviewed control SHA-256 707fcad468b95724d09031f
// dfe83c2103c1e7a7aa1330822e8bc973992f53559; export extent 11228128.
// Pin every byte before the bank, including padding, the 0x21 relative pointer,
// image size, and both signed array counts. No caller-supplied offsets/identity.
constexpr char kQualifiedPrefix[] =
    "\x00\x04\x0d\x02\x01\x06\x13\x02\x10\x03\x00\x00\x00\x00\x01\x00"
    "\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x03\x00\x00\x00\x0f\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x10\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x11\x00"
    "\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00"
    "\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x80\x07\x05\x62\x10\xea"
    "\x42\x01\x01\x01\xcd\xcc\x4c\x3f\x62\x10\xea\x42\x00\x00\x00\x00"
    "\x30\x53\xab\x00\x00\x00\x01\x00\x10\x00\x04\x00\x00\x00\x00\x00"
    "\x21\x00\x00\x00\x00\x00\x00\x00\x20\x53\xab\x00\x20\x53\xab\x00";
constexpr std::size_t kBankOffset = sizeof(kQualifiedPrefix) - 1;
constexpr std::size_t kBankBytes = 11227936;
constexpr std::size_t kTailBytes = 48;
constexpr std::size_t kArtifactBytes = kBankBytes + kTailBytes;
constexpr std::size_t kExportBytes = kBankOffset + kArtifactBytes;
static_assert(kBankOffset == 144 && kExportBytes == 11228128);

Status invalid(const char* message) {
    return Status::error(StatusCode::InvalidArgument, message);
}

Status validate_inputs(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement, bool fixed=true,
    MabfArtifactMetadata* replacement_metadata=nullptr) {
    if (source.size() != kExportBytes ||
        std::memcmp(source.data(), kQualifiedPrefix, kBankOffset) != 0) {
        return invalid("Unsupported native Music export header or fixed source-span geometry");
    }
    const auto& trailer = ff7r::piano::pipeline::mabf_scaffold::kTrailers[2];
    if (!std::equal(trailer.end() - kTailBytes, trailer.end(), source.end() - kTailBytes)) {
        return invalid("Unsupported native Music post-image suffix");
    }
    if (fixed && replacement.size() != kArtifactBytes) {
        return invalid("Native Music replacement must have the qualified equal bank geometry");
    }
    // The structural validator includes the 48-byte tail in its size/slot/CRC
    // checks. Validate the original span too, not merely the replacement artifact.
    const std::vector<std::uint8_t> original(source.begin() + kBankOffset, source.end());
    MabfArtifactMetadata metadata;
    auto status = validate_structural_mabf(original, &metadata);
    if (!status.ok()) return status;
    status = validate_structural_mabf(replacement, &metadata);
    if (!status.ok()) return status;
    if (replacement_metadata) *replacement_metadata=metadata;
    // The legacy fixed-control overload intentionally retains its timing policy.
    constexpr std::size_t slot_offsets[] = {0x460, 3743408, 7485696};
    constexpr unsigned char hca_geometry[] = {
        0x02, 0x00, 0xbb, 0x80, 0x00, 0x00, 0x15, 0x6f, 0x00, 0x80, 0x03, 0xf5};
    constexpr unsigned char relative_slots[] = {0x70, 0x1a, 0x39, 0x00, 0xc0, 0x34, 0x72, 0x00};
    if (std::memcmp(original.data() + 0x424, relative_slots, sizeof(relative_slots)) != 0 ||
        (fixed && std::memcmp(replacement.data() + 0x424, relative_slots, sizeof(relative_slots)) != 0)) {
        return invalid("Native Music requires the qualified mode-slot offsets");
    }
    for (const auto offset : slot_offsets) {
        if (std::memcmp(original.data() + offset + 12, hca_geometry, sizeof(hca_geometry)) != 0 ||
            (fixed && std::memcmp(replacement.data() + offset + 12, hca_geometry, sizeof(hca_geometry)) != 0)) {
            return invalid("Native Music requires the qualified source-frame and mode-slot geometry");
        }
    }
    if (!std::equal(source.end() - kTailBytes, source.end(), replacement.end() - kTailBytes)) {
        return invalid("Native Music replacement tail differs from preserved post-image suffix");
    }
    return Status::ok_status();
}

std::vector<std::uint8_t> older_prefix() {
    std::vector<std::uint8_t> b(kQualifiedPrefix,kQualifiedPrefix+kBankOffset);
    b[5]=4; b.erase(b.begin()+0x63); b.insert(b.begin()+0x7f,0);
    native_binary::put(b,0x79,5,2); return b;
}

struct OlderSource { std::size_t tail=0,volume=0; };
Status validate_older_inputs(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement,MabfArtifactMetadata& metadata,OlderSource& layout) {
    using namespace native_binary;
    if(source.size()>=10&&std::memcmp(source.data(),kQualifiedPrefix,10)==0) {
        auto status=validate_inputs(source,replacement,false,&metadata);
        if(status.ok()) layout={source.size()-kTailBytes,0x64}; return status;
    }
    if(source.size()<kBankOffset+40||source.size()>64*1024*1024) return invalid("Older Music source extent");
    auto expected=older_prefix();
    if(get(source,0x26)==14) for(auto at:{0x26,0x32,0x3e}) put(expected,at,get(expected,at)-1);
    for(std::size_t i=0;i<kBankOffset;++i) {
        const bool variable=(i>=0x5d&&i<0x61)||(i>=0x67&&i<0x6b)||
            (i>=0x6f&&i<0x73)||(i>=0x88&&i<0x90);
        if(!variable&&source[i]!=expected[i]) return invalid("Unsupported older Music reflected/maps/wrapper profile");
    }
    const auto duration=std::bit_cast<float>(std::uint32_t(get(source,0x5d)));
    if(!std::isfinite(duration)||duration<=0||get(source,0x5d)!=get(source,0x67)) return invalid("Older Music source durations");
    const auto n=get(source,0x88);
    if(!n||n>std::uint64_t(std::numeric_limits<std::int32_t>::max())-16||get(source,0x8c)!=n||
       get(source,0x6f)!=n+16||(source.size()!=kBankOffset+n+40&&source.size()!=kBankOffset+n+48)) return invalid("Older Music mapped image/count/capacity");
    const auto tail=std::size_t(kBankOffset+n);
    if(get(source,tail,8)||get(source,tail+8)||get(source,tail+12)!=1||get(source,tail+16)||
       get(source,tail+36)!=1||(source.size()-tail==48&&get(source,tail+40,8))) return invalid("Unsupported older Music cooked suffix branch");
    layout={tail,0x63};
    // The native bank ends before cooked serialization. The sidecar validator
    // expects its own scaffold tail, not the source export's cooked GUID.
    std::vector<std::uint8_t> bank(source.begin()+kBankOffset,source.begin()+tail);
    const auto& trailer=ff7r::piano::pipeline::mabf_scaffold::kTrailers[2];
    bank.insert(bank.end(),trailer.end()-kTailBytes,trailer.end());
    auto status=validate_structural_mabf(bank,&metadata); if(!status.ok()) return status;
    return validate_structural_mabf(replacement,&metadata);
}

} // namespace

float native_music_fixed_gain() noexcept {
    // Decode the same pinned bytes the exporter/bank builder preserve.
    const std::span<const std::uint8_t> prefix(
        reinterpret_cast<const std::uint8_t*>(kQualifiedPrefix), kBankOffset);
    const auto asset = std::bit_cast<float>(std::uint32_t(native_binary::get(prefix, 0x64)));
    const auto music = std::bit_cast<float>(std::uint32_t(native_binary::get(
        ff7r::piano::pipeline::mabf_scaffold::kHeader, 0x98)));
    return asset * music;
}

NativeMusicExportResult build_native_music_export(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement, const NativeMusicCueTiming& timing) {
    return build_native_music_export(source,replacement,timing,NativeMusicExportProfile::Current1005);
}

NativeMusicExportResult build_native_music_export(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement, const NativeMusicCueTiming& timing,NativeMusicExportProfile profile) {
    NativeMusicExportResult result;
    MabfArtifactMetadata metadata;
    OlderSource older;
    if(profile==NativeMusicExportProfile::Current1005) result.status=validate_inputs(source,replacement,false,&metadata);
    else if(profile==NativeMusicExportProfile::Older1004) result.status=validate_older_inputs(source,replacement,metadata,older);
    else result.status=invalid("Unsupported Music export build profile");
    if(!result.status.ok()) return result;
    const double seconds=timing.sample_rate?double(timing.logical_source_frames)/timing.sample_rate:0;
    const float serialized=float(seconds);
    if(!timing.logical_source_frames || timing.sample_rate!=metadata.sample_rate ||
       timing.logical_source_frames!=metadata.logical_source_frames ||
       !std::isfinite(timing.duration_seconds)||timing.duration_seconds<=0 ||
       !std::isfinite(serialized)||serialized<=0 ||
       std::abs(timing.duration_seconds-seconds)>std::numeric_limits<double>::epsilon()*4*std::max(1.0,seconds)) {
        result.status=invalid("Native Music logical cue frames/rate/duration disagree with validated MABF"); return result;
    }
    const auto bank=replacement.size()-kTailBytes;
    if(bank>std::size_t(std::numeric_limits<std::int32_t>::max())-16 ||
       bank>std::numeric_limits<std::size_t>::max()-kBankOffset-kTailBytes) {
        result.status=invalid("Native Music mapped image exceeds signed serialized bounds"); return result;
    }
    if(profile==NativeMusicExportProfile::Older1004) {
        // NativeAudioAssetProfile-OlderBuild: explicit fields select the cooked
        // nonstreaming/channel branch; StoreEntry exponent zero adds no padding.
        constexpr std::uint8_t header[]{0x00,0x04,0x0d,0x02,0x01,0x04,0x06,0x02,0x01,0x02,0x0a,0x02,0x01,0x02,0x0e,0x03};
        result.bytes.assign(160,0); std::copy(std::begin(header),std::end(header),result.bytes.begin());
        std::copy(source.begin()+0x0a,source.begin()+0x61,result.bytes.begin()+0x10);
        result.bytes[0x67]=source[0x61]; result.bytes[0x68]=source[0x62];
        result.bytes[0x69]=0; result.bytes[0x6a]=4;
        native_binary::put(result.bytes,0x6b,native_binary::get(source,older.volume));
        native_binary::put(result.bytes,0x63,std::bit_cast<std::uint32_t>(serialized));
        native_binary::put(result.bytes,0x73,std::bit_cast<std::uint32_t>(serialized));
        native_binary::put(result.bytes,0x7b,bank+16); native_binary::put(result.bytes,0x7f,0x10000);
        native_binary::put(result.bytes,0x83,16,2); native_binary::put(result.bytes,0x85,9,2);
        native_binary::put(result.bytes,0x90,0x21,8); native_binary::put(result.bytes,0x98,bank); native_binary::put(result.bytes,0x9c,bank);
        result.bytes.insert(result.bytes.end(),replacement.begin(),replacement.end()-kTailBytes);
        result.bytes.insert(result.bytes.end(),source.begin()+older.tail,source.begin()+older.tail+40);
        return result;
    }
    result.bytes.assign(source.begin(),source.begin()+kBankOffset);
    // Reflected profile: nested SoundIndexInfo Duration and inherited SoundBase
    // Duration; all fragment/mask/map/streaming/volume/GUID bytes stay unchanged.
    native_binary::put(result.bytes,0x5d,std::bit_cast<std::uint32_t>(serialized));
    native_binary::put(result.bytes,0x68,std::bit_cast<std::uint32_t>(serialized));
    native_binary::put(result.bytes,0x70,bank+16);
    native_binary::put(result.bytes,0x88,bank); native_binary::put(result.bytes,0x8c,bank);
    result.bytes.insert(result.bytes.end(),replacement.begin(),replacement.end()-kTailBytes);
    result.bytes.insert(result.bytes.end(),source.end()-kTailBytes,source.end());
    return result;
}

Status verify_native_music_export(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement, const NativeMusicCueTiming& timing,
    const std::vector<std::uint8_t>& candidate) {
    auto expected=build_native_music_export(source,replacement,timing);
    if(!expected.status.ok()) return expected.status;
    if(candidate!=expected.bytes) return invalid("Native Music export differs from qualified timing/image projection");
    return Status::ok_status();
}

Status verify_native_music_export(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement,const NativeMusicCueTiming& timing,
    NativeMusicExportProfile profile,const std::vector<std::uint8_t>& candidate) {
    auto expected=build_native_music_export(source,replacement,timing,profile);
    if(!expected.status.ok()) return expected.status;
    return candidate==expected.bytes?Status::ok_status():invalid("Native Music export differs from selected build profile");
}

NativeMusicExportResult build_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf) {
    NativeMusicExportResult result;
    result.status = validate_inputs(source_export, replacement_mabf);
    if (!result.status.ok()) return result;
    result.bytes = source_export;
    std::copy_n(replacement_mabf.begin(), kBankBytes, result.bytes.begin() + kBankOffset);
    return result;
}

Status verify_native_music_export(
    const std::vector<std::uint8_t>& source_export,
    const std::vector<std::uint8_t>& replacement_mabf,
    const std::vector<std::uint8_t>& candidate_export) {
    const auto status = validate_inputs(source_export, replacement_mabf);
    if (!status.ok()) return status;
    if (candidate_export.size() != kExportBytes ||
        !std::equal(source_export.begin(), source_export.begin() + kBankOffset, candidate_export.begin()) ||
        !std::equal(source_export.end() - kTailBytes, source_export.end(), candidate_export.end() - kTailBytes) ||
        !std::equal(replacement_mabf.begin(), replacement_mabf.begin() + kBankBytes,
            candidate_export.begin() + kBankOffset)) {
        return invalid("Native Music export differs from exact bank replacement or preserved wrapper");
    }
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
