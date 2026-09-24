#include "midi_chart_generator.h"

#include "midi_chart_compilation.h"
#include "midi_source_normalizer.h"

#include <cmath>
#include <utility>

namespace ff7rp::pipeline {

Status generate_notes_from_midi(
    const std::string& midi_path,
    const WavAudio& audio,
    const SongConfig& config,
    const NativeAssetCapabilities native_assets,
    std::vector<Note>* out_notes,
    MidiChartStats* out_stats,
    const std::vector<Note>* preferred_baseline,
    const std::size_t maximum_visible_rows) {
    if (!out_notes) return Status::error(StatusCode::InvalidArgument, "out_notes must not be null");
    out_notes->clear();
    if (out_stats) *out_stats = MidiChartStats{};
    if (config.difficulty < kLowestMidiDifficulty || config.difficulty > kHighestMidiDifficulty) {
        return Status::error(StatusCode::InvalidChart, "MIDI chart difficulty must be between 1 and 6");
    }
    if (!std::isfinite(config.midi_minimum_lead_in_seconds) ||
        config.midi_minimum_lead_in_seconds < 0.0 || config.midi_minimum_lead_in_seconds > 30.0) {
        return Status::error(StatusCode::InvalidChart,
            "MIDI minimum lead-in must be between 0 and 30 seconds");
    }
    NormalizedMidiSource normalized_source;
    const Status normalization_status = normalize_midi_source(midi_path, &normalized_source);
    if (!normalization_status.ok()) return normalization_status;

    return generate_notes_from_normalized_midi(normalized_source, audio, config, native_assets, out_notes,
        out_stats, preferred_baseline, maximum_visible_rows);
}

Status generate_notes_from_normalized_midi(
    const NormalizedMidiSource& normalized_source,
    const WavAudio& audio,
    const SongConfig& config,
    const NativeAssetCapabilities native_assets,
    std::vector<Note>* out_notes,
    MidiChartStats* out_stats,
    const std::vector<Note>* preferred_baseline,
    const std::size_t maximum_visible_rows) {
    if (!out_notes) return Status::error(StatusCode::InvalidArgument, "out_notes must not be null");
    out_notes->clear();
    if (out_stats) *out_stats = MidiChartStats{};
    if (config.difficulty < kLowestMidiDifficulty || config.difficulty > kHighestMidiDifficulty) {
        return Status::error(StatusCode::InvalidChart, "MIDI chart difficulty must be between 1 and 6");
    }
    if (!std::isfinite(config.midi_minimum_lead_in_seconds) ||
        config.midi_minimum_lead_in_seconds < 0.0 || config.midi_minimum_lead_in_seconds > 30.0) {
        return Status::error(StatusCode::InvalidChart,
            "MIDI minimum lead-in must be between 0 and 30 seconds");
    }

    MidiChartCompilationResult result = compile_normalized_midi_chart({
        normalized_source, audio, config, preferred_baseline, maximum_visible_rows, native_assets});
    *out_notes = std::move(result.notes);
    if (out_stats) *out_stats = std::move(result.stats);
    return std::move(result.status);
}

} // namespace ff7rp::pipeline
