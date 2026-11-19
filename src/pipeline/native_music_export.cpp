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

// Source maps and nested SoundIndexInfo have the same extent in both profiles.
constexpr std::size_t kSourceMaps = 0x0a;
constexpr std::size_t kSourceSoundIndexInfoDuration = 0x5d;
constexpr std::size_t kSourcePs5Streaming = 0x61;
constexpr std::size_t kSourceXsxStreaming = 0x62;

// Reflected fields (NativeMusicExportProfile-20261016; OlderBuild profile).
namespace current_sound {
constexpr std::size_t sw2_streaming = 0x63;
constexpr std::size_t volume = 0x64;
constexpr std::size_t duration = 0x68;
}
namespace older_source_sound {
constexpr std::size_t volume = 0x63;
constexpr std::size_t duration = 0x67;
}

// Mapped image wrapper and the root at the start of its signed-size payload.
constexpr std::size_t kMappedBankRootBytes = 16;
namespace source_mapped_bank {
constexpr std::size_t current_image_size = 0x70;
constexpr std::size_t older_image_size = 0x6f;
constexpr std::size_t older_padding_length = 0x79;
constexpr std::size_t older_padding_end = 0x7f;
constexpr std::size_t count = 0x88;
constexpr std::size_t capacity = 0x8c;
}

// The older target's selected cooked branch consumes only the first 40 bytes.
namespace older_cooked_suffix {
constexpr std::size_t vtable_and_script_name_fixups = 0;
constexpr std::size_t memory_image_name_fixups = 8;
constexpr std::size_t cooked = 12;
constexpr std::size_t compressed_format_count = 16;
constexpr std::size_t mapped_source_discriminator = 36;
constexpr std::size_t consumed_bytes = 40;
}

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
    b[5]=4; b.erase(b.begin()+current_sound::sw2_streaming);
    b.insert(b.begin()+source_mapped_bank::older_padding_end,0);
    native_binary::put(b,source_mapped_bank::older_padding_length,5,2); return b;
}

struct OlderSource { std::size_t tail=0,volume=0; };
Status validate_older_inputs(const std::vector<std::uint8_t>& source,
    const std::vector<std::uint8_t>& replacement,MabfArtifactMetadata& metadata,OlderSource& layout) {
    using namespace native_binary;
    if(source.size()>=10&&std::memcmp(source.data(),kQualifiedPrefix,10)==0) {
        auto status=validate_inputs(source,replacement,false,&metadata);
        if(status.ok()) layout={source.size()-kTailBytes,current_sound::volume}; return status;
    }
    if(source.size()<kBankOffset+older_cooked_suffix::consumed_bytes||source.size()>64*1024*1024)
        return invalid("Older Music source extent");
    auto expected=older_prefix();
    if(get(source,0x26)==14) for(auto at:{0x26,0x32,0x3e}) put(expected,at,get(expected,at)-1);
    for(std::size_t i=0;i<kBankOffset;++i) {
        const bool variable=(i>=kSourceSoundIndexInfoDuration&&i<kSourceSoundIndexInfoDuration+4)
            ||(i>=older_source_sound::duration&&i<older_source_sound::duration+4)
            ||(i>=source_mapped_bank::older_image_size&&i<source_mapped_bank::older_image_size+4)
            ||(i>=source_mapped_bank::count&&i<source_mapped_bank::capacity+4);
        if(!variable&&source[i]!=expected[i]) return invalid("Unsupported older Music reflected/maps/wrapper profile");
    }
    const auto duration=std::bit_cast<float>(std::uint32_t(get(source,kSourceSoundIndexInfoDuration)));
    if(!std::isfinite(duration)||duration<=0||get(source,kSourceSoundIndexInfoDuration)!=get(source,older_source_sound::duration))
        return invalid("Older Music source durations");
    const auto n=get(source,source_mapped_bank::count);
    if(!n||n>std::uint64_t(std::numeric_limits<std::int32_t>::max())-kMappedBankRootBytes
       ||get(source,source_mapped_bank::capacity)!=n||get(source,source_mapped_bank::older_image_size)!=n+kMappedBankRootBytes
       ||(source.size()!=kBankOffset+n+older_cooked_suffix::consumed_bytes&&source.size()!=kBankOffset+n+kTailBytes))
        return invalid("Older Music mapped image/count/capacity");
    const auto tail=std::size_t(kBankOffset+n);
    if(get(source,tail+older_cooked_suffix::vtable_and_script_name_fixups,8)
       ||get(source,tail+older_cooked_suffix::memory_image_name_fixups)
       ||get(source,tail+older_cooked_suffix::cooked)!=1||get(source,tail+older_cooked_suffix::compressed_format_count)
       ||get(source,tail+older_cooked_suffix::mapped_source_discriminator)!=1
       ||(source.size()-tail==kTailBytes&&get(source,tail+older_cooked_suffix::consumed_bytes,8)))
        return invalid("Unsupported older Music cooked suffix branch");
    layout={tail,older_source_sound::volume};
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
    const auto asset = std::bit_cast<float>(std::uint32_t(native_binary::get(prefix, current_sound::volume)));
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
    if(bank>std::size_t(std::numeric_limits<std::int32_t>::max())-kMappedBankRootBytes ||
       bank>std::numeric_limits<std::size_t>::max()-kBankOffset-kTailBytes) {
        result.status=invalid("Native Music mapped image exceeds signed serialized bounds"); return result;
    }
    if(profile==NativeMusicExportProfile::Older1004) {
        // NativeAudioAssetProfile-OlderBuild: explicit fields select the cooked
        // nonstreaming/channel branch; StoreEntry exponent zero adds no padding.
        constexpr std::uint8_t header[]{0x00,0x04,0x0d,0x02,0x01,0x04,0x06,0x02,0x01,0x02,0x0a,0x02,0x01,0x02,0x0e,0x03};
        constexpr std::size_t maps = sizeof(header), sound_index_info_duration = 0x63;
        constexpr std::size_t ps5_streaming = 0x67, xsx_streaming = 0x68;
        constexpr std::size_t streaming = 0x69, loading_behavior = 0x6a, volume = 0x6b, duration = 0x73;
        constexpr std::size_t image_size = 0x7b, version = 0x7f, alignment = 0x83, padding_length = 0x85;
        constexpr std::size_t frozen_pointer = 0x90, count = 0x98, capacity = 0x9c, bank_offset = 0xa0;
        result.bytes.assign(bank_offset,0); std::copy(std::begin(header),std::end(header),result.bytes.begin());
        std::copy(source.begin()+kSourceMaps,source.begin()+kSourcePs5Streaming,result.bytes.begin()+maps);
        result.bytes[ps5_streaming]=source[kSourcePs5Streaming]; result.bytes[xsx_streaming]=source[kSourceXsxStreaming];
        result.bytes[streaming]=0; result.bytes[loading_behavior]=4;
        native_binary::put(result.bytes,volume,native_binary::get(source,older.volume));
        native_binary::put(result.bytes,sound_index_info_duration,std::bit_cast<std::uint32_t>(serialized));
        native_binary::put(result.bytes,duration,std::bit_cast<std::uint32_t>(serialized));
        native_binary::put(result.bytes,image_size,bank+kMappedBankRootBytes); native_binary::put(result.bytes,version,0x10000);
        native_binary::put(result.bytes,alignment,16,2); native_binary::put(result.bytes,padding_length,9,2);
        native_binary::put(result.bytes,frozen_pointer,0x21,8);
        native_binary::put(result.bytes,count,bank); native_binary::put(result.bytes,capacity,bank);
        result.bytes.insert(result.bytes.end(),replacement.begin(),replacement.end()-kTailBytes);
        result.bytes.insert(result.bytes.end(),source.begin()+older.tail,source.begin()+older.tail+older_cooked_suffix::consumed_bytes);
        return result;
    }
    result.bytes.assign(source.begin(),source.begin()+kBankOffset);
    // Reflected profile: nested SoundIndexInfo Duration and inherited SoundBase
    // Duration; all fragment/mask/map/streaming/volume/GUID bytes stay unchanged.
    native_binary::put(result.bytes,kSourceSoundIndexInfoDuration,std::bit_cast<std::uint32_t>(serialized));
    native_binary::put(result.bytes,current_sound::duration,std::bit_cast<std::uint32_t>(serialized));
    native_binary::put(result.bytes,source_mapped_bank::current_image_size,bank+kMappedBankRootBytes);
    native_binary::put(result.bytes,source_mapped_bank::count,bank); native_binary::put(result.bytes,source_mapped_bank::capacity,bank);
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
