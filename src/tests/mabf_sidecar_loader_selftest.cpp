// MABF remains an offline/public artifact. Native runtime allocation/borrower
// and prefix tests were replaced by native_audio_adapter_selftest, not stubbed.
#include "game/mabf_sidecar_loader.h"
#include "pipeline/hca/hca_encoder.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"
#include "tests/test_support.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    require(bool(file),"fixture write failed");
}
void rejected(const std::filesystem::path& path,const char* status) {
    const auto result=ff7r::piano::game::load_sidecar_bytes_file(path.wstring());
    require(!result.ok && result.bytes.empty() && result.status==status,"malformed MABF accepted or wrong rejection");
}
std::uint32_t u32(const std::vector<std::uint8_t>& bytes,std::size_t offset) {
    std::uint32_t result=0;for(unsigned i=0;i<4;++i)result|=std::uint32_t(bytes[offset+i])<<(8*i);return result;
}
void set32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes[offset+i]=static_cast<std::uint8_t>(value>>(8*i));
}
}
int main() {
    try {
        ff7rp::tests::TemporaryDirectory temporary("ff7rp-mabf-sidecar-loader");
        HcaEncodeConfig config;config.target_samples=1;
        const auto hca=encode_hca_48k_stereo_256k(std::vector<std::int16_t>(2,0),config);
        const auto built=ff7rp::pipeline::build_mabf_from_mode_hca({&hca,&hca,&hca});
        require(built.status.ok() && built.release_valid,"production MABF build failed");
        ff7rp::pipeline::MabfArtifactMetadata metadata;
        require(ff7rp::pipeline::validate_structural_mabf(built.bytes,&metadata).ok(),"independent validation failed");
        const auto path=temporary.path()/"valid.mabf";write(path,built.bytes);
        const auto valid=ff7r::piano::game::load_sidecar_bytes_file(path.wstring());
        require(valid.ok && valid.status=="ok" && valid.bytes==built.bytes,"MABF byte contract changed");
        rejected({},"empty_path");rejected(temporary.path()/"missing.mabf","missing");
        const auto empty=temporary.path()/"empty.mabf";write(empty,{});rejected(empty,"size_too_small");
        auto truncated=built.bytes;truncated.pop_back();
        const auto truncated_path=temporary.path()/"truncated.mabf";write(truncated_path,truncated);
        rejected(truncated_path,"structural_validation_failed");
        const auto oversized_path=temporary.path()/"oversized.mabf";
        {std::ofstream oversized(oversized_path,std::ios::binary|std::ios::trunc);
         oversized.seekp(static_cast<std::streamoff>(ff7rp::pipeline::kMaxMabfBytes));oversized.put('\0');
         require(bool(oversized),"oversized fixture write failed");}
        rejected(oversized_path,"size_too_large");
        const auto hca_offset=ff7rp::pipeline::kMabfSlot0Offset;
        for(const std::size_t offset:{std::size_t{0x100},hca_offset+8,hca_offset+12,
            hca_offset+ff7rp::pipeline::kMabfHcaHeaderSize+2,hca_offset+hca.size()}) {
            auto bad=built.bytes;bad[offset]^=1;const auto mutation=temporary.path()/"mutation.mabf";
            write(mutation,bad);rejected(mutation,"structural_validation_failed");
        }
        constexpr std::size_t slot1=0x424,slot2=0x428,base=0x440;
        constexpr auto minimum=ff7rp::pipeline::kMabfHcaHeaderSize+ff7rp::pipeline::kMabfTrailerMetadataSize;
        const auto end=static_cast<std::uint32_t>(built.bytes.size()-base);
        struct Mutation {std::size_t field;std::uint32_t value;};
        const Mutation mutations[]{
            {slot1,u32(built.bytes,slot2)-static_cast<std::uint32_t>(minimum-1)},
            {slot2,end-static_cast<std::uint32_t>(minimum-1)},
            {slot1,0x1f},{slot2,u32(built.bytes,slot1)-1},
            {slot1,std::numeric_limits<std::uint32_t>::max()},
            {slot2,std::numeric_limits<std::uint32_t>::max()},
            {slot1,end+1},{slot2,end+1}};
        for(const auto& mutation:mutations) {
            auto bad=built.bytes;set32(bad,mutation.field,mutation.value);
            const auto bad_path=temporary.path()/"offset.mabf";write(bad_path,bad);
            rejected(bad_path,"structural_validation_failed");
        }
        std::cout<<"mabf_sidecar_loader_selftest ok\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
