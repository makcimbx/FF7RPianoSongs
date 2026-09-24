#pragma once

#include <string>
#include <vector>

#include "song_types.h"
#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

struct PitchMapEntry {
    std::string pitch;
    std::string monotone_id;
};

const std::vector<PitchMapEntry>& supported_pitch_map();
Status pitch_to_monotone_id(const std::string& pitch, std::string* out_monotone_id);
std::string beat_to_time_str(double beat, double bpm);
Status compile_chart(const SongConfig& config, NativeAssetCapabilities native_assets,
    CompiledChart* out_chart, DiagnosticChartRetention* out_diagnostic = nullptr,
    std::size_t input_row_limit = 0);
std::uint64_t diagnostic_descriptor_hash(
    const std::string& song_id, int difficulty, const CompiledChart& playable_chart,
    const DiagnosticChartRetention& diagnostic);
bool diagnostic_charts_equal(
    const DiagnosticChartRetention& left, const DiagnosticChartRetention& right);

} // namespace ff7rp::pipeline
