#include "startup/native_audio_preparation.h"
#include "pipeline/native_audio_artifacts.h"

#include <chrono>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    using namespace ff7r::piano;
    ff7rp::pipeline::OodleDecoder decoder;
    const auto cache_root = std::filesystem::temp_directory_path() / "native-preparation-cache";
    game::SongDescriptor descriptor;
    for (int mode = 0; mode != 4; ++mode) {
        descriptor.id = mode == 0 ? "" : "song";
        descriptor.base_slot = mode == 1 ? 1 : 0;
        descriptor.sidecar_path = mode == 2 ? L"relative.mabf" : L"";
        descriptor.native_audio = std::make_shared<const ff7rp::pipeline::NativeAudioArtifactSet>();
        if (startup::prepare_native_audio_descriptor(descriptor, "unsupported", {}, decoder, cache_root).ok() ||
            descriptor.native_audio) {
            std::cerr << "Invalid preparation retained artifact authority\n";
            return 1;
        }
    }
    const auto invalid_file = std::filesystem::temp_directory_path() /
        ("native-preparation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream output(invalid_file, std::ios::binary);
        output << "not a MABF";
    }
    descriptor.sidecar_path = invalid_file.wstring();
    const auto invalid = startup::prepare_native_audio_descriptor(descriptor, "unsupported", {}, decoder, cache_root);
    std::filesystem::remove(invalid_file);
    if (invalid.ok() || descriptor.native_audio) return 1;
    if (argc != 1 && argc != 5) return 2;
    if (argc == 5) {
        descriptor.id = "song-\xe6\x9b\xb2";
        auto status = ff7rp::pipeline::OodleDecoder::load(std::filesystem::absolute(argv[2]), &decoder);
        if (!status.ok()) { std::cerr << status.message << '\n'; return 1; }
        descriptor.sidecar_path = std::filesystem::absolute(argv[3]).wstring();
        const auto explicit_cache = std::filesystem::path(descriptor.sidecar_path).parent_path() / "native-prepared-output";
        status = startup::prepare_native_audio_descriptor(descriptor, argv[4],
            std::filesystem::absolute(argv[1]), decoder, explicit_cache);
        if (!status.ok() || !descriptor.native_audio) {
            std::cerr << status.message << '\n';
            return 1;
        }
        if (descriptor.native_audio->container_prefix.parent_path().parent_path() != explicit_cache ||
            descriptor.native_audio->identity != descriptor.id) return 1;
    }
    std::cout << "native_audio_preparation_selftest passed\n";
}
