#include "game/synthetic_extended_chart_model.h"

#include <cmath>

namespace ff7r::piano::game::synthetic_model {
namespace {
bool restricted(const std::vector<SourceRow>& rows) {
    if (rows.size() != kPlayableRows) return false;
    float previous = -1;
    bool prefix_assignments[256]{};
    uint8_t prefix_lookup_path = 2;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (!row.monotone || row.chord || row.group || row.camera_transition || row.ignore_sound
            || row.strength != 0 || row.fname == 0 || row.assignment == 8 || row.lookup_path > 1
            || !std::isfinite(row.time) || row.time < previous) return false;
        if (index < kNativeRows) {
            if (index && row.lookup_path != prefix_lookup_path) return false;
            if (!index) prefix_lookup_path = row.lookup_path;
            prefix_assignments[row.assignment] = true;
        }
        previous = row.time;
    }
    return rows.back().lookup_path == prefix_lookup_path
        && prefix_assignments[rows.back().assignment];
}
}

Result run(const BuildRequest& request, Chart& chart) {
    Result result;
    const bool gate = request.rows && request.experiment_enabled && request.verified_1005
        && request.exact_caller && request.exact_header && request.exact_thread
        && request.depth_one && request.exact_generation && request.global_claim
        && restricted(*request.rows);
    if (!gate) { result.forwarded = true; return result; }
    result.substituted = true;
    if (request.second_reserve_hit) { result.forwarded = true; return result; }
    if (request.reserve_capacity < kPlayableRows || request.reserve_capacity > 1024) return result;

    chart.storage.clear(); chart.storage.reserve(request.reserve_capacity); chart.capacity = request.reserve_capacity;
    for (std::size_t i = 0; i < kNativeRows; ++i)
        chart.storage.push_back({i, static_cast<uint32_t>(2 * i), (*request.rows)[i].time, true});
    chart.count = kNativeRows; chart.max_time = request.rows->at(kNativeRows - 1).time;
    if (request.failure == FailurePoint::PrefixValidation) return result;
    result.prefix_validated = true;
    if (request.failure == FailurePoint::FNameFind) return result;

    chart.storage.push_back({kNativeRows, 1024, request.rows->back().time, false});
    chart.tail_constructor_entered = true; result.tail_constructed = true;
    const auto cleanup = [&] { chart.storage.resize(kNativeRows); chart.tail_destructed = true; };
    if (request.failure == FailurePoint::Constructor || request.failure == FailurePoint::ConstructorValidation) {
        cleanup(); return result;
    }
    if (request.failure == FailurePoint::CallbackBuild || request.failure == FailurePoint::CallbackValidation) {
        cleanup(); return result;
    }
    chart.storage.back().callback_valid = true; result.callback_validated = true;
    const float old_max = chart.max_time; chart.max_time = request.rows->back().time;
    if (request.failure == FailurePoint::MaxTimeValidation) {
        chart.max_time = old_max; cleanup(); result.max_restore_proved = true; return result;
    }
    if (request.failure == FailurePoint::MaxTimeRestoreFailure) {
        cleanup(); result.route_blocked = true; return result;
    }
    chart.count = kPlayableRows; result.count_committed = true;
    if (request.failure == FailurePoint::PostCountExternalDrift) {
        chart.count = 77; chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PostCountRestoreFailure) {
        chart.ownership_preserved = true; result.route_blocked = true; return result;
    }
    if (request.failure == FailurePoint::PostCountValidation) {
        chart.count = kNativeRows; result.count_restore_proved = true;
        chart.max_time = old_max; result.max_restore_proved = true;
        cleanup(); result.rolled_back = true; return result;
    }
    return result;
}

void model_next_parser_reset(Chart& chart) {
    chart.storage.clear(); chart.count = 0; chart.capacity = 0;
}
} // namespace ff7r::piano::game::synthetic_model
