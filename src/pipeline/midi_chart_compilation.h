#pragma once

#include <cstddef>
#include <set>
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

// Offline-only witness, never serialized into descriptors or caches.
struct MidiRepresentativeRootRepair {
    bool applied = false;
    MidiSourceIdentity removed, inserted;
    long long radius_before = 0, radius_after = 0;
    double salience_delta = 0.0, switch_penalty_delta = 0.0;
};

struct MidiChartCompilationResult {
    Status status = Status::ok_status();
    std::vector<Note> notes;
    MidiChartStats stats;
    MidiRepresentativeRootRepair representative_repair;
};

MidiChartCompilationResult compile_normalized_midi_chart(
    const MidiChartCompilationRequest& request);

// Descriptive soft ranking cost for chronological required inputs, not physical
// movement or a feasibility threshold. Consecutive group followers are excluded.
double calculate_midi_assignment_switch_penalty(const std::vector<Note>& notes, double bpm);

// Generated LH witnesses must describe exactly the unmuted native sounds and
// belong to the immutable normalized source, not merely share pitch classes.
bool midi_chord_has_exact_source_witnesses(const Note& note,
    const std::vector<MidiSourceIdentity>& witnesses,
    const std::set<MidiSourceIdentity>& source_identities,
    NativeAssetCapabilities native_assets);

} // namespace ff7rp::pipeline
