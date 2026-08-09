#include "pipeline/audio_artifact_builder.h"
#include "pipeline/wav_reader.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: hca_parity_fixture <clean.wav> <output.mabf.bin> [mode0.wav]\n";
        return 2;
    }

    ff7rp::pipeline::WavAudio audio;
    const auto read_status = ff7rp::pipeline::read_wav_file(argv[1], &audio);
    if (!read_status.ok()) {
        std::cerr << "input rejected: " << read_status.message << '\n';
        return 3;
    }
    ff7rp::pipeline::WavAudio mode0;
    if (argc == 4) {
        const auto mode0_status = ff7rp::pipeline::read_wav_file(argv[3], &mode0);
        if (!mode0_status.ok()) {
            std::cerr << "Mode0 input rejected: " << mode0_status.message << '\n';
            return 3;
        }
    }
    const auto built = ff7rp::pipeline::build_audio_mabf(
        audio, argc == 4 ? &mode0 : nullptr, argc == 4);
    if (!built.status.ok() || !built.release_valid) {
        std::cerr << "MABF generation failed: " << built.status.message << '\n';
        return 4;
    }

    std::ofstream output(std::filesystem::path(argv[2]), std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(built.bytes.data()),
        static_cast<std::streamsize>(built.bytes.size()));
    if (!output) {
        std::cerr << "failed to write output\n";
        return 5;
    }
    std::cout << "wrote " << built.bytes.size() << " bytes from " << audio.frame_count()
              << " source frames\n";
    return 0;
}
