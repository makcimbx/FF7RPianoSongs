#include "pipeline/native_audio_cache.h"
#include "pipeline/native_iostore_source.h"
#include "pipeline/native_asset_hash.h"
#include "pipeline/mabf_builder.h"
#include "pipeline/hca/hca_encoder.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

using namespace ff7rp::pipeline;
using Bytes=std::vector<std::uint8_t>;
namespace {
int fail(const std::string& s){std::cerr<<s<<'\n';return 1;}
std::string hex(const std::array<std::uint8_t,32>& b){std::string s;for(auto v:b){s+="0123456789abcdef"[v>>4];s+="0123456789abcdef"[v&15];}return s;}
bool compressed_alias_check(const std::filesystem::path& path,const NativeIoChunkId& id,
    const Bytes& expected,const OodleDecoder& decoder) {
    using namespace ff7rp::pipeline::native_binary;
    std::ifstream f(path,std::ios::binary); Bytes original(std::istreambuf_iterator<char>(f),{});
    auto n=get(original,24); std::size_t selected=0;
    for(;selected<n;++selected) if(std::equal(id.begin(),id.end(),original.begin()+144+selected*12)) break;
    if(selected==n) return false;
    auto be5=[](const std::uint8_t* p){std::uint64_t v=0;for(int i=0;i<5;++i)v=(v<<8)|p[i];return v;};
    auto at=144+n*12+selected*10; auto offset=be5(original.data()+at); auto length=be5(original.data()+at+5);
    auto block_at=144+n*22+(offset/65536)*12;
    auto disk=get(original,block_at,5),compressed=get(original,block_at+5,3),decoded=get(original,block_at+8,3);
    if(length>decoded-offset%65536||original[block_at+11]!=1) return false;
    auto cas=path; cas.replace_extension(".ucas"); std::ifstream c(cas,std::ios::binary); c.seekg(std::streamoff(disk));
    Bytes encoded(compressed); if(!c.read(reinterpret_cast<char*>(encoded.data()),std::streamsize(encoded.size()))) return false;
    Bytes toc(144+22+12+32+4+33,0); std::memcpy(toc.data(),"-==--==--==--==-",16);
    toc[16]=2;put(toc,20,144);put(toc,24,1);put(toc,28,1);put(toc,32,12);put(toc,36,1);put(toc,40,32);put(toc,44,65536);put(toc,48,4);toc[80]=9;
    std::copy(id.begin(),id.end(),toc.begin()+144);
    for(int i=0;i<5;++i){toc[156+i]=std::uint8_t((offset%65536)>>(8*(4-i)));toc[161+i]=std::uint8_t(length>>(8*(4-i)));}
    put(toc,171,compressed,3);put(toc,174,decoded,3);toc[177]=1;
    std::memcpy(toc.data()+178,"oodle",5);
    auto alias=encoded; auto s=extract_native_chunk(toc,alias,id,alias,&decoder);
    if(!s.ok()||alias!=expected) return false;
    alias=toc; s=extract_native_chunk(alias,encoded,id,alias,&decoder);
    if(!s.ok()||alias!=expected) return false;
    toc[178]='z';alias={1}; s=extract_native_chunk(toc,encoded,id,alias,&decoder);
    return !s.ok()&&alias.empty();
}
}
int main(int argc,char** argv) {
    NativeAudioCacheRequest r; OodleDecoder decoder; NativeAudioArtifactSet out; out.identity="must-clear";
    if(prepare_native_audio_cache(r,decoder,out).ok()||!out.identity.empty()) return fail("Invalid automatic request published");
    if(argc==1){std::cout<<"native_audio_cache_selftest passed (external extraction not requested)\n";return 0;}
    if(argc!=4) return fail("Usage: native_audio_cache_selftest [absolute-paks absolute-companion absolute-cache-root]");
    r.source_paks=argv[1]; r.cache_root=argv[3]; r.build_id="ff7rebirth-steam-win64-6a16ced2"; r.identity="automatic-original-generated-song"; r.base_score_name="Journey";
    auto s=OodleDecoder::load(argv[2],&decoder); if(!s.ok()) return fail(s.message);
    auto retained=decoder; decoder=OodleDecoder{}; // Copied decoder keeps authenticated companion alive.
    constexpr NativeIoChunkId ids[]{
        {0x80,0xbd,0x41,0x80,0xba,0x7f,0x81,0xe1,0,0,0,2},
        {0x7a,0x80,0x41,0xd3,0x1d,0x69,0x1a,0x3d,0,0,0,2},
        {0x77,0x3d,0x97,0x22,0x0e,0xbd,0xe2,0x26,0,0,0,2},
        {0xed,0x31,0x3f,0xea,0x77,0x6e,0x3a,0xdd,0,0,0,2}};
    // Reviewed raw stock chunk goldens, hashes only (no redistributed assets).
    constexpr const char* goldens[]{
        "707fcad468b95724d09031fdfe83c2103c1e7a7aa1330822e8bc973992f53559",
        "6d7ce8582def003fe46e76b653b0e9f0294e25eff7ac9bb8dcb77d98976d580e",
        "b7184b8520c1eae265dd30ba5c6f4f072ec0b160516d7770caca9f22f96f5dad",
        "a2dc74150a2d50f8f2e33e35c898fe64a370dc06a3ee0714c817ffa0aebfeffa"};
    for(std::size_t i=0;i<4;++i) {
        Bytes chunk;
        auto toc=r.source_paks/(i==0?"pakchunk8-WindowsNoEditor.utoc":"pakchunk3-WindowsNoEditor.utoc");
        if(read_native_stored_chunk(toc,ids[i],chunk).ok()||!chunk.empty()) return fail("Compressed stock accepted without explicit decoder");
        s=read_native_chunk(toc,ids[i],chunk,&retained); if(!s.ok()) return fail(s.message);
        if(hex(native_sha256(chunk))!=goldens[i]) return fail("Installed decoded chunk differs from reviewed raw golden");
        if(i==2&&!compressed_alias_check(toc,ids[i],chunk,retained)) return fail("Compressed extraction alias/unknown-codec checks");
    }
    // Actual encoder output from original silence, not synthetic CRC-only HCA.
    HcaEncodeConfig config; config.target_samples=48001;
    std::vector<std::int16_t> pcm(config.target_samples*2,0);
    auto hca=encode_hca_48k_stereo_256k(pcm,config);
    auto mabf=build_mabf_from_mode_hca({&hca,&hca,&hca}); if(!mabf.status.ok()) return fail(mabf.status.message);
    r.mabf=std::move(mabf.bytes); r.timing={config.target_samples,48000,double(config.target_samples)/48000};
    s=prepare_native_audio_cache(r,retained,out); if(!s.ok()) return fail(s.message);
    auto canonical=[](const NativeAudioArtifactSet& a) {
        return a.music_name.rfind("bgm_",0)==0&&a.music_name!=a.bgm_key&&
            a.packages[0].object_path=="/Game/Sound/BGM/"+a.music_name+"."+a.music_name;
    };
    if(!canonical(out)) return fail("Automatic Music is outside native queue loading convention");
    // This installation supplies known-current donors, not older stock bytes.
    // Exercise the explicit qualified current-donor -> older target conversion.
    auto older=r; older.build_id="ff7rebirth-steam-win64-68fd6fde";
    NativeAudioArtifactSet old_output;
    auto old_status=prepare_native_audio_cache(older,retained,old_output);
    if(!old_status.ok()||!canonical(old_output)||old_output.container_prefix==out.container_prefix||
       !validate_native_audio_artifacts(older,retained,old_output).ok()) return fail("Automatic older source/profile projection: "+old_status.message);
    for(std::size_t i=0;i<4;++i) if(old_output.packages[i].package_id==out.packages[i].package_id) return fail("Cross-build package identity reused");
    s=validate_native_audio_artifacts(r,retained,out); if(!s.ok()) return fail(s.message);
    if(out.base_score_name!=r.base_score_name) return fail("Authenticated source carrier was not retained");
    auto wrong_carrier=out; wrong_carrier.base_score_name="UnverifiedCarrier";
    if(validate_native_audio_artifacts(r,retained,wrong_carrier).ok()) return fail("Forged source carrier accepted");
    auto mtime=std::filesystem::last_write_time(out.files[2].path); NativeAudioArtifactSet again;
    s=prepare_native_audio_cache(r,retained,again);
    if(!s.ok()||out.container_prefix!=again.container_prefix||mtime!=std::filesystem::last_write_time(out.files[2].path)) return fail("Automatic cache reuse rewrote retained bytes");
    auto invalid=r; invalid.timing.logical_source_frames++;
    if(prepare_native_audio_cache(invalid,retained,again).ok()||!again.identity.empty()) return fail("Mismatching source timing published");
    invalid=r; invalid.source_paks="relative";
    if(prepare_native_audio_cache(invalid,retained,again).ok()) return fail("Relative source location accepted");
    auto forged=out; forged.packages[0].package_id^=1;
    if(validate_native_audio_artifacts(r,retained,forged).ok()) return fail("Forged automatic cache descriptor accepted");
    auto longer=r; config.target_samples=240001; pcm.assign(config.target_samples*2,0);
    hca=encode_hca_48k_stereo_256k(pcm,config); mabf=build_mabf_from_mode_hca({&hca,&hca,&hca});
    if(!mabf.status.ok()) return fail(mabf.status.message);
    longer.mabf=std::move(mabf.bytes);longer.timing={config.target_samples,48000,double(config.target_samples)/48000};
    s=prepare_native_audio_cache(longer,retained,again);
    if(!s.ok()||!canonical(again)||again.container_prefix==out.container_prefix||again.container_id==out.container_id) return fail("Changed generated audio reused retained cohort identity");
    for(std::size_t i=0;i<4;++i) if(out.packages[i].package_id==again.packages[i].package_id) return fail("Changed audio reused retained package ID");
    if(!validate_native_audio_artifacts(r,retained,out).ok()||!validate_native_audio_artifacts(longer,retained,again).ok()) return fail("Changed audio invalidated retained cohort");
    auto older_long=longer;older_long.build_id=older.build_id;NativeAudioArtifactSet old_long_output;
    s=prepare_native_audio_cache(older_long,retained,old_long_output);
    if(!s.ok()||old_long_output.container_prefix==old_output.container_prefix||!validate_native_audio_artifacts(older_long,retained,old_long_output).ok()||
       !validate_native_audio_artifacts(older,retained,old_output).ok()) return fail("Older short/long immutable cohort identity");
    std::cout<<"Automatic older short-song cohort: "<<old_output.container_prefix.string()<<'\n';
    std::cout<<"Automatic older longer-song cohort: "<<old_long_output.container_prefix.string()<<'\n';
    std::cout<<"Automatic generated longer-song cohort: "<<again.container_prefix.string()<<'\n';
    std::cout<<"All four installed decoded chunks match goldens\nAutomatic generated short-song cohort: "<<out.container_prefix.string()<<"\nnative_audio_cache_selftest passed\n";
    return 0;
}
