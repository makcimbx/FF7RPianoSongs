#pragma once

#include <cstddef>
#include <vector>

#include "midi_chart_generator.h"
#include "midi_source_normalizer.h"
#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

// Internal immutable input for the offline chart-compilation package. Callers must
// first apply the generator facade's difficulty and minimum-lead-in preflight.
// The package owns all derived notes, diagnostics, and failure witnesses in its result.
struct MidiChartCompilationRequest {
    const NormalizedMidiSource& source;
    const WavAudio& audio;
    const SongConfig& config;
    const std::vector<Note>* preferred_baseline = nullptr;
    std::size_t maximum_visible_rows = 0;
    NativeAssetCapabilities native_assets;
};

struct MidiChartCompilationResult {
    Status status = Status::ok_status();
    std::vector<Note> notes;
    MidiChartStats stats;
};

MidiChartCompilationResult compile_normalized_midi_chart(
    const MidiChartCompilationRequest& request);

} // namespace ff7rp::pipeline
