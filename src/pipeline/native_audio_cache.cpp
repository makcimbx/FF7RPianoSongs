#include "native_audio_cache.h"
#include "native_iostore_source.h"

namespace ff7rp::pipeline {
namespace {
Status extract_request(const NativeAudioCacheRequest& r,const OodleDecoder& decoder,
    NativeAudioCohortRequest& out) {
    if(!r.source_paks.is_absolute()||!r.cache_root.is_absolute()||
        (r.build_id!="ff7rebirth-steam-win64-6a16ced2"&&r.build_id!="ff7rebirth-steam-win64-68fd6fde"))
        return Status::error(StatusCode::InvalidArgument,"Native audio cache requires absolute Paks/cache paths and supported build");
    out.build_id=r.build_id; out.identity=r.identity; out.base_score_name=r.base_score_name;
    out.mabf=r.mabf; out.timing=r.timing;
    constexpr NativeIoChunkId ids[]{
        {0x80,0xbd,0x41,0x80,0xba,0x7f,0x81,0xe1,0,0,0,2},
        {0x7a,0x80,0x41,0xd3,0x1d,0x69,0x1a,0x3d,0,0,0,2},
        {0x77,0x3d,0x97,0x22,0x0e,0xbd,0xe2,0x26,0,0,0,2},
        {0xed,0x31,0x3f,0xea,0x77,0x6e,0x3a,0xdd,0,0,0,2}};
    for(std::size_t i=0;i<4;++i) {
        auto s=read_native_chunk(r.source_paks/(i==0?"pakchunk8-WindowsNoEditor.utoc":"pakchunk3-WindowsNoEditor.utoc"),ids[i],out.source_zen[i],&decoder);
        if(!s.ok()) return Status::error(s.code,"Native source role "+std::to_string(i)+": "+s.message);
    }
    return Status::ok_status();
}
}
Status prepare_native_audio_cache(const NativeAudioCacheRequest& request,
    const OodleDecoder& decoder,NativeAudioArtifactSet& out) {
    out={}; NativeAudioCohortRequest decoded;
    auto s=extract_request(request,decoder,decoded); if(!s.ok()) return s;
    return prepare_native_audio_cohort(decoded,request.cache_root,out);
}
Status validate_native_audio_artifacts(const NativeAudioCacheRequest& request,
    const OodleDecoder& decoder,const NativeAudioArtifactSet& artifacts) {
    NativeAudioCohortRequest decoded; auto s=extract_request(request,decoder,decoded);
    if(!s.ok()) return s;
    const auto expected_prefix=request.cache_root/artifacts.container_prefix.parent_path().filename()/artifacts.container_prefix.filename();
    if(artifacts.container_prefix!=expected_prefix)
        return Status::error(StatusCode::InvalidArgument,"Native cache root differs from request");
    return verify_native_audio_cohort(decoded,artifacts);
}
} // namespace ff7rp::pipeline
