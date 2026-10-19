#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "song_types.h"

namespace ff7rp::pipeline {

inline constexpr std::uint32_t kTranscriptionSampleRate = 16000;

struct TranscribedNote {
    double start = 0.0;
    double end = 0.0;
    int pitch = 0;
    int program = 0;
};

// Turns 16 kHz mono samples into pitched, non-drum notes.
using AudioTranscriber = std::function<Status(std::span<const float> mono_samples, std::vector<TranscribedNote>* out_notes)>;

// While a transcriber is set, a song without song.mid gets one transcribed from its base audio.
// Set it before song discovery starts; it is not synchronized with running loads.
void set_audio_transcriber(AudioTranscriber transcriber);
bool audio_transcriber_available();

// Decodes audio_path, runs the transcriber, and writes the notes as midi_path.
Status transcribe_audio_to_midi(const std::string& audio_path, const std::filesystem::path& midi_path);

// One track per program at 120 BPM, so each source instrument is its own generator stream.
Status write_transcription_midi(const std::vector<TranscribedNote>& notes, const std::filesystem::path& midi_path);

} // namespace ff7rp::pipeline
