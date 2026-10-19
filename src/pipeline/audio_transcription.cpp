#include "audio_transcription.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <utility>

#include "MidiFile.h"
#include "audio_reader.h"

#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#include <miniaudio.h>

namespace ff7rp::pipeline {
namespace {

AudioTranscriber g_transcriber;

Status downmix_to_transcription_rate(const WavAudio& audio, std::vector<float>* out_mono) {
    std::vector<float> mono(audio.frame_count());
    for (std::size_t frame = 0; frame < mono.size(); ++frame) {
        mono[frame] = 0.5f * (audio.stereo_samples[frame * 2] + audio.stereo_samples[frame * 2 + 1]);
    }
    const ma_resampler_config config = ma_resampler_config_init(
        ma_format_f32, 1, audio.sample_rate, kTranscriptionSampleRate, ma_resample_algorithm_linear);
    ma_resampler resampler{};
    if (ma_resampler_init(&config, nullptr, &resampler) != MA_SUCCESS) {
        return Status::error(StatusCode::InvalidAudio, "failed to initialize the transcription resampler");
    }
    ma_uint64 frames_in = mono.size();
    ma_uint64 frames_out = 0;
    ma_resampler_get_expected_output_frame_count(&resampler, frames_in, &frames_out);
    out_mono->resize(static_cast<std::size_t>(frames_out));
    const ma_result result = ma_resampler_process_pcm_frames(&resampler, mono.data(), &frames_in, out_mono->data(), &frames_out);
    ma_resampler_uninit(&resampler, nullptr);
    if (result != MA_SUCCESS) {
        return Status::error(StatusCode::InvalidAudio, "failed to resample audio for transcription");
    }
    out_mono->resize(static_cast<std::size_t>(frames_out));
    return Status::ok_status();
}

} // namespace

void set_audio_transcriber(AudioTranscriber transcriber) {
    g_transcriber = std::move(transcriber);
}

bool audio_transcriber_available() {
    return static_cast<bool>(g_transcriber);
}

Status transcribe_audio_to_midi(const std::string& audio_path, const std::filesystem::path& midi_path) {
    if (!g_transcriber) {
        return Status::error(StatusCode::NotFound, "no audio transcriber is available");
    }
    std::vector<float> mono;
    {
        WavAudio audio;
        Status status = read_audio_file(audio_path, &audio);
        if (!status.ok()) return status;
        status = downmix_to_transcription_rate(audio, &mono);
        if (!status.ok()) return status;
    }
    std::vector<TranscribedNote> notes;
    const Status status = g_transcriber(mono, &notes);
    if (!status.ok()) return status;
    return write_transcription_midi(notes, midi_path);
}

Status write_transcription_midi(const std::vector<TranscribedNote>& notes, const std::filesystem::path& midi_path) {
    constexpr int kTicksPerQuarter = 480;
    constexpr double kTicksPerSecond = kTicksPerQuarter * 2.0;
    smf::MidiFile midi;
    midi.setTicksPerQuarterNote(kTicksPerQuarter);
    midi.addTempo(0, 0, 120.0);
    std::map<int, int> track_of_program;
    std::size_t written = 0;
    for (const TranscribedNote& note : notes) {
        if (!(note.start >= 0.0 && note.end > note.start) || note.pitch < 0 || note.pitch > 127) continue;
        auto [entry, added] = track_of_program.try_emplace(note.program, 0);
        // Tracks start at 1 after the tempo track; channel 9 would read as percussion.
        const int track = added ? midi.addTrack() : entry->second;
        const int channel = (track - 1) % 15 < 9 ? (track - 1) % 15 : (track - 1) % 15 + 1;
        if (added) {
            entry->second = track;
            midi.addPatchChange(track, 0, channel, note.program & 0x7f);
        }
        const int start = static_cast<int>(std::lround(note.start * kTicksPerSecond));
        const int end = std::max(start + 1, static_cast<int>(std::lround(note.end * kTicksPerSecond)));
        midi.addNoteOn(track, start, channel, note.pitch, 90);
        midi.addNoteOff(track, end, channel, note.pitch);
        ++written;
    }
    if (written == 0) {
        return Status::error(StatusCode::InvalidAudio, "audio transcription found no pitched notes");
    }
    midi.sortTracks();

    // A partial file must never look like an authored song.mid.
    std::filesystem::path partial = midi_path;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out || !midi.write(out) || !out.flush()) {
            return Status::error(StatusCode::IoError, "failed to write transcribed MIDI: " + partial.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(partial, midi_path, ec);
    if (ec) {
        return Status::error(StatusCode::IoError, "failed to publish transcribed MIDI: " + midi_path.string());
    }
    return Status::ok_status();
}

} // namespace ff7rp::pipeline
