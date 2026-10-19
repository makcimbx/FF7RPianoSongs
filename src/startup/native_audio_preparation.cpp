#include "native_audio_preparation.h"

#include <fstream>

#include "pipeline/mabf_builder.h"
#include "pipeline/pipeline_limits.h"
#include "pipeline/native_audio_cache.h"

namespace ff7r::piano::startup {

ff7rp::pipeline::Status prepare_native_audio_descriptor(game::SongDescriptor& descriptor,
    const std::string_view build_id, const std::filesystem::path& source_paks,
    const ff7rp::pipeline::OodleDecoder& decoder, const std::filesystem::path& cache_root,
    ff7rp::pipeline::NativeAudioCacheSession* session)
{
    namespace pipeline = ff7rp::pipeline;
    descriptor.native_audio.reset();
    if (!cache_root.is_absolute())
        return pipeline::Status::error(pipeline::StatusCode::InvalidArgument,
            "Native cache requires an absolute shared output directory");
    if (descriptor.id.empty() || descriptor.base_slot != 0 || descriptor.sidecar_path.empty())
        return pipeline::Status::error(pipeline::StatusCode::InvalidArgument,
            "Native descriptor requires song identity and the existing Journey carrier");
    const std::filesystem::path sidecar(descriptor.sidecar_path);
    std::error_code error;
    const auto bytes = std::filesystem::file_size(sidecar, error);
    if (error || !sidecar.is_absolute() || bytes == 0 || bytes > pipeline::kMaxMabfBytes)
        return pipeline::Status::error(pipeline::StatusCode::IoError, "Native sidecar size/path invalid");
    pipeline::NativeAudioCacheRequest request;
    request.mabf.resize(static_cast<std::size_t>(bytes));
    std::ifstream input(sidecar, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(request.mabf.data()), static_cast<std::streamsize>(bytes)) ||
        input.peek() != std::char_traits<char>::eof())
        return pipeline::Status::error(pipeline::StatusCode::IoError, "Native sidecar read/extent changed");
    pipeline::MabfArtifactMetadata metadata;
    auto status = pipeline::validate_structural_mabf(request.mabf, &metadata);
    if (!status.ok()) return status;
    request.build_id = build_id;
    request.identity = descriptor.id;
    request.base_score_name = "Journey";
    request.source_paks = source_paks;
    request.cache_root = cache_root;
    request.timing = {metadata.logical_source_frames, metadata.sample_rate,
        metadata.sample_rate ? static_cast<double>(metadata.logical_source_frames) / metadata.sample_rate : 0.0};
    pipeline::NativeAudioArtifactSet artifacts;
    status = pipeline::prepare_native_audio_cache(request, decoder, artifacts, session);
    if (!status.ok()) return status;
    descriptor.native_audio = std::make_shared<const pipeline::NativeAudioArtifactSet>(std::move(artifacts));
    return pipeline::Status::ok_status();
}

} // namespace ff7r::piano::startup
