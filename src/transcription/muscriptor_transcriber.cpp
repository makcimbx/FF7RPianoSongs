#include "transcription/muscriptor_transcriber.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include <muscriptor/muscriptor.hpp>

#include "pipeline/audio_transcription.h"

namespace ff7rp::transcription {
namespace {

using pipeline::Status;
using pipeline::StatusCode;

// ggml slows severalfold when two runs overlap, and songs load on parallel workers.
std::mutex g_run_mutex;

} // namespace

bool register_muscriptor_transcriber(std::filesystem::path model_path, const int cpu_threads,
    std::function<void(const std::string&)> log) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(model_path, ec)) return false;
    pipeline::set_audio_transcriber([model_path = std::move(model_path), cpu_threads, log = std::move(log)](
        const std::span<const float> samples, std::vector<pipeline::TranscribedNote>* out_notes) -> Status {
        std::lock_guard lock(g_run_mutex);
        const auto started = std::chrono::steady_clock::now();
        auto transcriber = msl::Transcriber::load(model_path, {});
        if (!transcriber) {
            return Status::error(StatusCode::IoError,
                std::string("failed to load the transcription model: ") + msl::describe(transcriber.error()));
        }
        msl::TranscribeOptions options;
        options.n_threads = cpu_threads;
        // The minigame plays piano; masking other instruments keeps them out of the decoding.
        options.instruments = {msl::InstrumentGroup::AcousticPiano};
        const auto notes = transcriber->transcribe(samples, options);
        if (!notes) {
            return Status::error(StatusCode::InvalidAudio,
                std::string("audio transcription failed: ") + msl::describe(notes.error()));
        }
        for (const msl::Note& note : *notes) {
            if (!note.is_drum) out_notes->push_back({note.onset, note.offset, note.pitch, note.program});
        }
        if (log) {
            std::ostringstream line;
            line << "[transcription] device=\"" << transcriber->device().name << "\" backend="
                 << transcriber->device().backend << " threads=" << cpu_threads
                 << " audio_seconds=" << static_cast<double>(samples.size()) / pipeline::kTranscriptionSampleRate
                 << " elapsed_seconds="
                 << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
                 << " notes=" << out_notes->size();
            log(line.str());
        }
        return Status::ok_status();
    });
    return true;
}

} // namespace ff7rp::transcription
