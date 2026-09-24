#include "midi_chart_compilation.h"
#include "midi_analysis_core.h"
#include "native_chord_constituents.h"
#include "pipeline_limits.h"
#include "chart_compiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <execution>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ff7rp::pipeline {
namespace {

NoteValueOverride exact_midi_note_value(
    const MidiSourceIdentity& source, const int ticks_per_quarter)
{
    NoteValueOverride result;
    const std::int64_t ticks = static_cast<std::int64_t>(source.end_tick) - source.tick;
    if (ticks <= 0 || ticks_per_quarter <= 0) return result;
    struct Candidate { std::int64_t numerator; std::int64_t denominator; NativeNoteValue value; };
    constexpr Candidate candidates[] = {
        {4, 1, {0, 0}}, {6, 1, {0, 1}}, {2, 1, {1, 0}}, {3, 1, {1, 1}},
        {1, 1, {2, 0}}, {3, 2, {2, 1}}, {1, 2, {3, 0}}, {3, 4, {3, 1}},
        {1, 4, {4, 0}}, {3, 8, {4, 1}},
        // Preserve ordinary eighth/sixteenth precedence for the dotted overlaps.
        {1, 3, {5, 0}}, {1, 2, {5, 1}}, {1, 6, {6, 0}}, {1, 4, {6, 1}},
    };
    for (const Candidate& candidate : candidates) {
        if (ticks * candidate.denominator ==
            static_cast<std::int64_t>(ticks_per_quarter) * candidate.numerator) {
            result.value = candidate.value;
            result.provided = true;
            break;
        }
    }
    return result;
}

NoteValueOverride exact_midi_chord_note_value(
    const std::vector<MidiSourceIdentity>& sources, const int ticks_per_quarter)
{
    if (sources.empty()) return {};
    const NoteValueOverride first = exact_midi_note_value(sources.front(), ticks_per_quarter);
    if (!first.provided) return {};
    for (std::size_t index = 1; index < sources.size(); ++index) {
        if (exact_midi_note_value(sources[index], ticks_per_quarter) != first) return {};
    }
    return first;
}

constexpr double kAudioPlaybackDelaySeconds = 0.007;
constexpr double kPrimaryVoiceEvidence = 0.46;
constexpr double kComparisonEpsilon = 1e-9;

constexpr std::array<const char*, 12> kPitchClasses{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

using SourceIdentity = MidiSourceIdentity;
using MidiNoteEvent = NormalizedMidiNoteEvent;
using TempoChange = MidiTempoChange;

using MeterChange = MidiMeterChange;

struct Profile {
    double right_seconds = 0.5;
    double right_beats = 1.0;
    double chord_seconds = 1.5;
    double chord_beats = 2.0;
    double evidence = 0.3;
    double target_actions_per_minute = 120.0;
    double target_tolerance = 0.06;
    double maximum_joint_strain_p95 = 0.0;
    double maximum_joint_strain_peak = 0.0;
    struct SkillRoute {
        std::array<std::size_t, 5> maximum_window_actions{};
        std::size_t maximum_quarter_second_stream_actions = 0;
        double maximum_quarter_second_stream_duration = 0.0;
        std::size_t maximum_half_second_stream_actions = 0;
        double maximum_half_second_stream_duration = 0.0;
        std::size_t maximum_jack_run = 0;
        std::size_t maximum_reversal_run = 0;
        double maximum_rapid_movement_p90 = 0.0;
        double maximum_rapid_movement = 0.0;
        std::size_t maximum_octave_movements_in_five_seconds = 0;
        std::size_t maximum_large_reversals_in_five_seconds = 0;
        double maximum_octave_movement_rate = 1.0;
        double maximum_right_fatigue = 0.0;
        double maximum_left_fatigue = 0.0;
        double maximum_hand_imbalance = 1.0;
        double maximum_rhythm_irregularity_p90 = 0.0;
        double maximum_rhythm_irregularity = 0.0;
        const char* name = "";
        double evidence_margin = 1.0;
    };
    std::array<SkillRoute, 2> routes{};
    std::size_t route_count = 0;
};

enum class ChordQuality : std::size_t {
    Major,
    Minor,
    Diminished,
    Sus4,
    Dominant7,
    Minor7,
    Major7,
    Ninth,
    Minor9,
    MinorMajor7,
    Count,
};

struct ChordTemplate {
    ChordQuality quality;
    std::array<int, 5> intervals;
    std::size_t size;
};

struct ChordMatch {
    std::string id;
    int root = -1;
    ChordQuality quality = ChordQuality::Count;
};

constexpr std::array<ChordTemplate, 10> kChordTemplates{{
    {ChordQuality::Major,        {{0, 4, 7, 0, 0}},     3},
    {ChordQuality::Minor,        {{0, 3, 7, 0, 0}},     3},
    {ChordQuality::Diminished,   {{0, 3, 6, 0, 0}},     3},
    {ChordQuality::Sus4,         {{0, 5, 7, 0, 0}},     3},
    {ChordQuality::Dominant7,    {{0, 4, 7, 10, 0}},    4},
    {ChordQuality::Minor7,       {{0, 3, 7, 10, 0}},    4},
    {ChordQuality::Major7,       {{0, 4, 7, 11, 0}},    4},
    {ChordQuality::Ninth,        {{0, 2, 4, 7, 10}},    5},
    {ChordQuality::Minor9,       {{0, 2, 3, 7, 10}},    5},
    {ChordQuality::MinorMajor7,  {{0, 3, 7, 11, 0}},    4},
}};
using ChordIds = std::array<const char*, static_cast<std::size_t>(ChordQuality::Count)>;
constexpr std::array<ChordIds, 12> kNativeChordIds{{
    ChordIds{{"pca_C",  "pca_C_m",  "pca_C_dim",  "pca_C_sus4",  "pca_C_7",  "pca_C_m7",  "pca_C_Maj7",  "pca_C_9",  "pca_C_m9",  "pca_C_mM7"}},
    ChordIds{{"pca_Cs", "pca_Cs_m", "pca_Cs_dim", "pca_Db_sus4", "pca_Cs_7", "pca_Cs_m7", "pca_Db_Maj7", "pca_Cs_9", "pca_Cs_m9", "pca_Cs_mM7"}},
    ChordIds{{"pca_D",  "pca_D_m",  "pca_D_dim",  "pca_D_sus4",  "pca_D_7",  "pca_D_m7",  "pca_D_Maj7",  "pca_D_9",  "pca_D_m9",  "pca_D_mM7"}},
    ChordIds{{"pca_Eb", "pca_Eb_m", "pca_Eb_dim", "pca_Eb_sus4", "pca_Eb_7", "pca_Eb_m7", "pca_Eb_Maj7", "pca_Eb_9", "pca_Eb_m9", "pca_Eb_mM7"}},
    ChordIds{{"pca_E",  "pca_E_m",  "pca_E_dim",  "pca_E_sus4",  "pca_E_7",  "pca_E_m7",  "pca_E_Maj7",  "pca_E_9",  "pca_E_m9",  "pca_E_mM7"}},
    ChordIds{{"pca_F",  "pca_F_m",  "pca_F_dim",  "pca_F_sus4",  "pca_F_7",  "pca_F_m7",  "pca_F_Maj7",  "pca_F_9",  "pca_F_m9",  "pca_F_mM7"}},
    ChordIds{{"pca_Fs", "pca_Fs_m", "pca_Fs_dim", "pca_Fs_sus4", "pca_Fs_7", "pca_Fs_m7", "pca_Gb_Maj7", "pca_Fs_9", "pca_Fs_m9", "pca_Fs_mM7"}},
    ChordIds{{"pca_G",  "pca_G_m",  "pca_G_dim",  "pca_G_sus4",  "pca_G_7",  "pca_G_m7",  "pca_G_Maj7",  "pca_G_9",  "pca_G_m9",  "pca_G_mM7"}},
    ChordIds{{"pca_Ab", "pca_Ab_m", "pca_Ab_dim", "pca_Ab_sus4", "pca_Ab_7", "pca_Ab_m7", "pca_Ab_Maj7", "pca_Ab_9", "pca_Ab_m9", "pca_Ab_mM7"}},
    ChordIds{{"pca_A",  "pca_A_m",  "pca_A_dim",  "pca_A_sus4",  "pca_A_7",  "pca_A_m7",  "pca_A_Maj7",  "pca_A_9",  "pca_A_m9",  "pca_A_mM7"}},
    ChordIds{{"pca_Bb", "pca_Bb_m", "pca_Bb_dim", "pca_Bb_sus4", "pca_Bb_7", "pca_Bb_m7", "pca_Bb_Maj7", "pca_Bb_9", "pca_Bb_m9", "pca_Bb_mM7"}},
    ChordIds{{"pca_B",  "pca_B_m",  "pca_B_dim",  "pca_B_sus4",  "pca_B_7",  "pca_B_m7",  "pca_B_Maj7",  "pca_B_9",  "pca_B_m9",  "pca_B_mM7"}},
}};

struct SelectionStats {
    std::size_t evidence_rejections = 0;
    std::size_t cooldown_rejections = 0;
    std::size_t burst_rejections = 0;
    std::size_t retention_rejections = 0;
    std::size_t action_rate_rejections = 0;
};

Profile profile_for_difficulty(const int difficulty) {
    using Route = Profile::SkillRoute;
    const Route journey{{3, 4, 7, 14, 25}, 5, 1.25, 18, 6.0, 3, 3,
        7.0, 19.0, 1, 2, 0.08, 7.2, 7.2, 0.99, 3.0, 5.0, "Journey", 1.15};
    const Route tifa{{3, 5, 8, 17, 31}, 7, 1.75, 22, 7.0, 4, 4,
        7.0, 21.0, 2, 3, 0.08, 8.0, 8.0, 0.99, 3.5, 5.5, "Tifa", 1.15};
    // Lv.3 keeps Barret's dense route and Synco's recovery-rich route coherent.
    // A chart must satisfy one whole route; maxima are never mixed component-wise.
    const Route barret{{4, 7, 14, 30, 56}, 12, 3.0, 40, 8.75, 5, 5,
        7.0, 24.0, 2, 3, 0.05, 10.5, 10.5, 0.99, 4.0, 6.0, "Barret", 1.05};
    const Route synco{{3, 6, 10, 21, 38}, 8, 2.0, 19, 5.5, 4, 6,
        5.0, 21.0, 2, 4, 0.05, 9.0, 9.0, 0.99, 4.5, 6.5, "Synco", 1.15};
    const Route difficult{{4, 8, 15, 32, 60}, 13, 3.25, 44, 10.0, 6, 6,
        9.0, 28.0, 3, 4, 0.08, 11.5, 11.5, 0.99, 4.5, 7.0, "Difficult", 1.05};
    const Route aerith{{4, 7, 12, 25, 46}, 10, 2.5, 28, 7.5, 5, 7,
        7.0, 26.0, 3, 5, 0.08, 10.5, 10.5, 0.99, 5.0, 7.5, "Aerith", 1.05};
    const Route fighters{{5, 9, 17, 35, 66}, 15, 3.75, 50, 11.5, 7, 7,
        10.0, 31.0, 4, 5, 0.10, 12.5, 12.5, 0.99, 5.5, 8.0, "Fighters", 1.10};
    const Route owa{{5, 10, 19, 39, 73}, 17, 4.25, 58, 13.0, 8, 8,
        12.0, 36.0, 5, 6, 0.12, 13.5, 13.5, 0.99, 6.0, 9.0, "OneWingedAngel", 1.15};
    switch (difficulty) {
    case 1: return {0.11, 1.00, 3.00, 4.50, 0.38, 78.0, 0.06,
        2.80, 3.35, {journey, {}}, 1};
    case 2: return {0.11, 0.90, 2.00, 3.00, 0.36, 90.0, 0.06,
        3.00, 3.55, {tifa, {}}, 1};
    case 3: return {0.09, 0.72, 1.50, 2.25, 0.30, 104.0, 0.06,
        4.25, 5.30, {barret, synco}, 2};
    case 4: return {0.08, 0.58, 1.25, 1.75, 0.24, 120.0, 0.06,
        5.05, 5.75, {difficult, aerith}, 2};
    case 5: return {0.08, 0.45, 1.10, 1.50, 0.18, 138.0, 0.06,
        5.20, 6.00, {fighters, {}}, 1};
    case 6: return {0.08, 0.35, 0.90, 1.00, 0.12, 158.0, 0.06,
        5.40, 6.25, {owa, {}}, 1};
    default: return {};
    }
}

std::string pitch_name(const int midi) {
    return std::string(kPitchClasses[static_cast<std::size_t>(midi % 12)]) +
        std::to_string(midi / 12 - 1);
}

std::string generated_pitch_name(
    const int midi, const MidiAccidentalOrientation orientation) {
    if (orientation != MidiAccidentalOrientation::Flat) return pitch_name(midi);
    constexpr std::array<const char*, 12> flat_pitch_classes{
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    return std::string(flat_pitch_classes[static_cast<std::size_t>(midi % 12)]) +
        std::to_string(midi / 12 - 1);
}

void measure_shared_audio_prominence(
    const WavAudio& audio,
    const std::vector<std::vector<Attack>*>& groups,
    const double alignment_seconds) {
    if (audio.sample_rate == 0 || audio.channels != 2) return;
    std::vector<std::pair<Attack*, double>> measured;
    for (std::vector<Attack>* group : groups) {
        if (!group) continue;
        for (Attack& attack : *group) {
            measured.emplace_back(&attack,
                measure_midi_attack_audio_novelty(audio, attack, alignment_seconds));
        }
    }
    if (measured.empty()) return;
    std::vector<double> ordered;
    ordered.reserve(measured.size());
    for (const auto& entry : measured) ordered.push_back(entry.second);
    std::sort(ordered.begin(), ordered.end());
    const double position = 0.9 * static_cast<double>(ordered.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - lower;
    const double scale = ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction;
    for (auto& entry : measured) {
        entry.first->audio_prominence = scale > 0.0 ? std::min(entry.second / scale, 2.0) : 0.0;
    }
}

double attack_evidence(const Attack& attack) {
    const double velocity = attack.event.source.velocity / 127.0;
    const double audio = std::clamp(attack.audio_prominence / 2.0, 0.0, 1.0);
    return 0.28 * velocity + 0.24 * audio + 0.20 * attack.metric_accent +
        0.28 * attack.melody_evidence - (attack.fallback ? 0.04 : 0.0);
}

double attack_score(const Attack& attack) {
    const double duration = std::min(attack.end - attack.start, 1.0);
    return 1.8 * attack.event.source.velocity / 127.0 + 0.6 * duration +
        2.5 * attack.audio_prominence + 0.9 * attack.metric_accent +
        1.2 * attack.melody_evidence + 1.5 * attack.rhythmic_quality -
        (attack.fallback ? 0.35 : 0.0);
}

bool attack_less(const Attack& a, const Attack& b) {
    if (std::fabs(a.start - b.start) > kComparisonEpsilon) return a.start < b.start;
    if (std::fabs(a.beat - b.beat) > kComparisonEpsilon) return a.beat < b.beat;
    return a.event.source < b.event.source;
}

bool alternate_monotone_eligible(const int midi_pitch) {
    const int pitch_class = (midi_pitch % 12 + 12) % 12;
    const int octave = midi_pitch / 12 - 1;
    // Match the stock assignment inventory; do not plan absent boundary IDs.
    return octave >= 2 && octave <= 6 && (pitch_class == 0 || pitch_class == 1);
}

std::map<SourceIdentity, bool> plan_alternate_monotones(std::vector<Attack> canonical_right) {
    std::sort(canonical_right.begin(), canonical_right.end(), attack_less);
    std::vector<std::size_t> eligible;
    for (std::size_t index = 0; index < canonical_right.size(); ++index) {
        if (alternate_monotone_eligible(canonical_right[index].event.source.pitch)) eligible.push_back(index);
    }

    struct Cost {
        int boundary = std::numeric_limits<int>::max();
        int transition = std::numeric_limits<int>::max();
        int alternate = std::numeric_limits<int>::max();
    };
    const auto less_cost = [](const Cost& left, const Cost& right) {
        return std::tie(left.boundary, left.transition, left.alternate) <
            std::tie(right.boundary, right.transition, right.alternate);
    };
    const auto local_boundary_cost = [&](const std::size_t canonical_index, const bool alternate) {
        int lower_neighbors = 0;
        int higher_neighbors = 0;
        const int pitch = canonical_right[canonical_index].event.source.pitch;
        if (canonical_index > 0) {
            lower_neighbors += canonical_right[canonical_index - 1].event.source.pitch < pitch ? 1 : 0;
            higher_neighbors += canonical_right[canonical_index - 1].event.source.pitch > pitch ? 1 : 0;
        }
        if (canonical_index + 1 < canonical_right.size()) {
            lower_neighbors += canonical_right[canonical_index + 1].event.source.pitch < pitch ? 1 : 0;
            higher_neighbors += canonical_right[canonical_index + 1].event.source.pitch > pitch ? 1 : 0;
        }
        // The high-C sector is ergonomic at an upper boundary; the ordinary
        // low-C sector is ergonomic at a lower boundary.
        return alternate ? higher_neighbors : lower_neighbors;
    };

    std::vector<std::array<Cost, 2>> costs(eligible.size());
    std::vector<std::array<int, 2>> parents(eligible.size(), std::array<int, 2>{-1, -1});
    for (std::size_t item = 0; item < eligible.size(); ++item) {
        for (int state = 0; state < 2; ++state) {
            Cost local{local_boundary_cost(eligible[item], state != 0), 0, state};
            if (item == 0) {
                costs[item][state] = local;
                continue;
            }
            for (int previous = 0; previous < 2; ++previous) {
                Cost candidate = costs[item - 1][previous];
                candidate.boundary += local.boundary;
                candidate.alternate += local.alternate;
                if (eligible[item] == eligible[item - 1] + 1 && state != previous) ++candidate.transition;
                if (parents[item][state] == -1 || less_cost(candidate, costs[item][state])) {
                    costs[item][state] = candidate;
                    parents[item][state] = previous;
                }
            }
        }
    }

    std::map<SourceIdentity, bool> result;
    if (eligible.empty()) return result;
    int state = less_cost(costs.back()[1], costs.back()[0]) ? 1 : 0;
    for (std::size_t reverse = eligible.size(); reverse > 0; --reverse) {
        const std::size_t item = reverse - 1;
        result.emplace(canonical_right[eligible[item]].event.source, state != 0);
        state = parents[item][state];
    }
    return result;
}

bool attack_better(const Attack& a, const Attack& b) {
    const double a_score = attack_score(a);
    const double b_score = attack_score(b);
    if (std::fabs(a_score - b_score) > kComparisonEpsilon) return a_score > b_score;
    return a.event.source < b.event.source;
}

std::vector<int> spacing_predecessors(
    const std::vector<Attack>& attacks,
    const double minimum_seconds) {
    std::vector<int> predecessors(attacks.size(), -1);
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        for (std::size_t reverse = i; reverse > 0; --reverse) {
            const std::size_t j = reverse - 1;
            if (attacks[i].start - attacks[j].start >= minimum_seconds - kComparisonEpsilon) {
                predecessors[i] = static_cast<int>(j);
                break;
            }
        }
    }
    return predecessors;
}

bool matching_rhythmic_interval(const double first, const double second) {
    if (first <= kComparisonEpsilon || second <= kComparisonEpsilon) return false;
    return std::fabs(first - second) <=
        std::max(1.0 / 48.0, 0.12 * std::min(first, second));
}

void annotate_rhythmic_quality(
    std::vector<Attack>* attacks,
    const double minimum_seconds,
    const double preferred_beats) {
    if (!attacks || attacks->empty() || preferred_beats <= 0.0) return;
    const auto interval = [&](const std::size_t first, const std::size_t second, double* beats) {
        if (second >= attacks->size() || first >= second || !beats) return false;
        const Attack& begin = (*attacks)[first];
        const Attack& end = (*attacks)[second];
        *beats = end.beat - begin.beat;
        return end.start - begin.start >= minimum_seconds - kComparisonEpsilon &&
            *beats > kComparisonEpsilon;
    };
    for (std::size_t index = 0; index < attacks->size(); ++index) {
        double previous = 0.0;
        double next = 0.0;
        const bool has_previous = index > 0 && interval(index - 1, index, &previous);
        const bool has_next = interval(index, index + 1, &next);
        bool continuous = has_previous && has_next && matching_rhythmic_interval(previous, next);
        if (!continuous && has_next) {
            double following = 0.0;
            continuous = interval(index + 1, index + 2, &following) &&
                matching_rhythmic_interval(next, following);
        }
        if (!continuous && has_previous && index > 1) {
            double preceding = 0.0;
            continuous = interval(index - 2, index - 1, &preceding) &&
                matching_rhythmic_interval(preceding, previous);
        }
        if (continuous) {
            (*attacks)[index].rhythmic_quality = 1.0;
            continue;
        }
        double nearest = std::numeric_limits<double>::infinity();
        if (has_previous) nearest = std::min(nearest, previous);
        if (has_next) nearest = std::min(nearest, next);
        if (nearest < preferred_beats) {
            (*attacks)[index].rhythmic_quality =
                -std::clamp((preferred_beats - nearest) / preferred_beats, 0.0, 1.0);
        }
    }
}

std::size_t maximum_spaced_count(
    const std::vector<Attack>& attacks,
    const std::vector<int>& predecessors) {
    std::vector<std::size_t> counts(attacks.size() + 1, 0);
    for (std::size_t prefix = 1; prefix <= attacks.size(); ++prefix) {
        const std::size_t index = prefix - 1;
        counts[prefix] = std::max(counts[prefix - 1],
            1 + counts[static_cast<std::size_t>(predecessors[index] + 1)]);
    }
    return counts.back();
}

Status select_exact_count(
    const std::vector<Attack>& attacks,
    const std::vector<int>& predecessors,
    const std::size_t count,
    std::vector<Attack>* out) {
    if (!out) return Status::error(StatusCode::InvalidArgument, "out must not be null");
    out->clear();
    if (count == 0) return Status::ok_status();
    const double impossible = -std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> scores(count + 1,
        std::vector<double>(attacks.size() + 1, impossible));
    std::vector<std::vector<unsigned char>> choices(count + 1,
        std::vector<unsigned char>(attacks.size() + 1, 0));
    std::fill(scores[0].begin(), scores[0].end(), 0.0);
    for (std::size_t selected = 1; selected <= count; ++selected) {
        for (std::size_t prefix = 1; prefix <= attacks.size(); ++prefix) {
            scores[selected][prefix] = scores[selected][prefix - 1];
            const std::size_t index = prefix - 1;
            const std::size_t previous_prefix = static_cast<std::size_t>(predecessors[index] + 1);
            const double previous = scores[selected - 1][previous_prefix];
            const double candidate = previous + attack_score(attacks[index]);
            if (std::isfinite(previous) && candidate > scores[selected][prefix] + kComparisonEpsilon) {
                scores[selected][prefix] = candidate;
                choices[selected][prefix] = 1;
            }
        }
    }
    if (!std::isfinite(scores[count][attacks.size()])) {
        return Status::error(StatusCode::InvalidChart, "cannot satisfy MIDI reduction constraints");
    }
    std::size_t prefix = attacks.size();
    std::size_t selected = count;
    while (selected > 0) {
        if (choices[selected][prefix] != 0u) {
            const std::size_t index = prefix - 1;
            out->push_back(attacks[index]);
            prefix = static_cast<std::size_t>(predecessors[index] + 1);
            --selected;
        } else {
            --prefix;
        }
    }
    std::reverse(out->begin(), out->end());
    return Status::ok_status();
}

Status select_right_attacks(
    std::vector<Attack> attacks,
    const Profile& profile,
    const double source_duration,
    std::vector<Attack>* out,
    SelectionStats* stats) {
    std::sort(attacks.begin(), attacks.end(), attack_less);
    attacks.erase(std::unique(attacks.begin(), attacks.end(), [](const Attack& a, const Attack& b) {
        return a.event.source == b.event.source;
    }), attacks.end());
    std::vector<Attack> eligible;
    for (const Attack& attack : attacks) {
        if (attack_evidence(attack) + kComparisonEpsilon >= profile.evidence) eligible.push_back(attack);
    }
    if (stats) stats->evidence_rejections = attacks.size() - eligible.size();
    if (eligible.empty() && !attacks.empty()) {
        eligible.push_back(*std::max_element(attacks.begin(), attacks.end(),
            [](const Attack& a, const Attack& b) { return attack_better(b, a); }));
        if (stats && stats->evidence_rejections > 0) --stats->evidence_rejections;
    }
    annotate_rhythmic_quality(&eligible, profile.right_seconds, profile.right_beats);
    const std::vector<int> predecessors = spacing_predecessors(eligible, profile.right_seconds);
    const std::size_t spaced_count = maximum_spaced_count(eligible, predecessors);
    if (stats) stats->cooldown_rejections = eligible.size() - spaced_count;
    const std::size_t retention_cap = eligible.size() <= 8 ? eligible.size() :
        std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(eligible.size() * (1.0 - profile.target_tolerance))));
    const std::size_t after_retention = std::min(spaced_count, retention_cap);
    if (stats) stats->retention_rejections = spaced_count - after_retention;
    const double active_span = eligible.size() > 1 ? eligible.back().start - eligible.front().start : source_duration;
    const std::size_t rate_cap = eligible.size() <= 8 ? spaced_count :
        std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(
            std::max(0.0, active_span) * profile.target_actions_per_minute / 60.0)));
    const std::size_t selected_count = std::min(after_retention, rate_cap);
    if (stats) stats->action_rate_rejections = after_retention - selected_count;
    Status status = select_exact_count(eligible, predecessors, selected_count, out);
    if (!status.ok()) return status;
    return Status::ok_status();
}

Status select_chords(
    std::vector<Attack> attacks,
    const Profile& profile,
    std::vector<Attack>* out) {
    std::sort(attacks.begin(), attacks.end(), attack_less);
    annotate_rhythmic_quality(&attacks, profile.chord_seconds, profile.chord_beats);
    const std::vector<int> predecessors = spacing_predecessors(attacks, profile.chord_seconds);
    const std::size_t count = maximum_spaced_count(attacks, predecessors);
    return select_exact_count(attacks, predecessors, count, out);
}

void ensure_tail_coverage(
    const std::vector<Attack>& candidates,
    const Profile& profile,
    std::vector<Attack>* selected) {
    if (!selected || candidates.empty()) return;
    const std::size_t target_count = selected->size();
    const Attack& tail = *std::max_element(candidates.begin(), candidates.end(),
        [](const Attack& a, const Attack& b) { return attack_less(a, b); });
    const auto already_selected = std::find_if(selected->begin(), selected->end(),
        [&](const Attack& attack) { return attack.event.source == tail.event.source; });
    if (already_selected != selected->end()) return;
    selected->erase(std::remove_if(selected->begin(), selected->end(), [&](const Attack& attack) {
        return tail.start - attack.start < profile.right_seconds - kComparisonEpsilon;
    }), selected->end());
    selected->push_back(tail);
    std::sort(selected->begin(), selected->end(), attack_less);

    // Vanilla charts allow musical rests, but their longest internal gap is
    // about eight seconds. Bridge larger reduction-created gaps with real
    // source-voice attacks so native blank detection cannot end the chart
    // before the retained final MIDI onset.
    constexpr double kMaximumInternalPromptGapSeconds = 8.0;
    bool inserted = true;
    while (inserted) {
        inserted = false;
        for (std::size_t index = 1; index < selected->size(); ++index) {
            const Attack& previous = (*selected)[index - 1];
            const Attack& next = (*selected)[index];
            if (next.start - previous.start <= kMaximumInternalPromptGapSeconds + kComparisonEpsilon) continue;

            const Attack* bridge = nullptr;
            for (const Attack& candidate : candidates) {
                if (candidate.start <= previous.start + profile.right_seconds - kComparisonEpsilon
                    || candidate.start >= next.start - profile.right_seconds + kComparisonEpsilon
                    || candidate.start > previous.start + kMaximumInternalPromptGapSeconds + kComparisonEpsilon) {
                    continue;
                }
                if (!bridge || attack_less(*bridge, candidate)) bridge = &candidate;
            }
            if (!bridge) continue;
            selected->insert(selected->begin() + static_cast<std::ptrdiff_t>(index), *bridge);
            inserted = true;
            break;
        }
    }

    while (selected->size() > target_count && selected->size() > 2) {
        std::size_t worst = selected->size();
        for (std::size_t index = 1; index + 1 < selected->size(); ++index) {
            const Attack& attack = (*selected)[index];
            if (attack.event.source == tail.event.source ||
                (*selected)[index + 1].start - (*selected)[index - 1].start >
                    kMaximumInternalPromptGapSeconds + kComparisonEpsilon) {
                continue;
            }
            if (worst == selected->size() || attack_better((*selected)[worst], attack)) {
                worst = index;
            }
        }
        if (worst == selected->size()) break;
        selected->erase(selected->begin() + static_cast<std::ptrdiff_t>(worst));
    }
}

const char* native_chord_id(
    const int root, const ChordQuality quality, const bool flat_db_major,
    const NativeAssetCapabilities native_assets) {
    if (native_assets.has_verified_pca_db_voicing() && flat_db_major &&
        root == 1 && quality == ChordQuality::Major) return "pca_Db";
    return kNativeChordIds[static_cast<std::size_t>(root)][static_cast<std::size_t>(quality)];
}

ChordMatch infer_native_chord_match(
    const std::set<int>& fresh_pitch_classes, const bool flat_db_major,
    const NativeAssetCapabilities native_assets) {
    if (fresh_pitch_classes.size() < 3) return {};
    std::vector<std::pair<int, ChordQuality>> exact_matches;
    for (int root = 0; root < 12; ++root) {
        for (const ChordTemplate& chord : kChordTemplates) {
            if (fresh_pitch_classes.size() != chord.size) continue;
            std::set<int> expected;
            for (std::size_t i = 0; i < chord.size; ++i) {
                expected.insert((root + chord.intervals[i]) % 12);
            }
            if (expected == fresh_pitch_classes) exact_matches.emplace_back(root, chord.quality);
        }
    }
    if (exact_matches.size() != 1) return {};
    const auto [root, quality] = exact_matches.front();
    const char* id = native_chord_id(root, quality, flat_db_major, native_assets);
    if (!id) return {};
    return {id, root, quality};
}

bool native_sound_pitch_class(const std::string_view sound, int* out) {
    if (!out || sound.size() != 3u || sound[2] < '0' || sound[2] > '9') return false;
    int pitch_class = 0;
    switch (sound[0]) {
    case 'C': pitch_class = 0; break;
    case 'D': pitch_class = 2; break;
    case 'E': pitch_class = 4; break;
    case 'F': pitch_class = 5; break;
    case 'G': pitch_class = 7; break;
    case 'A': pitch_class = 9; break;
    case 'B': pitch_class = 11; break;
    default: return false;
    }
    if (sound[1] == 's') ++pitch_class;
    else if (sound[1] == 'b') --pitch_class;
    else if (sound[1] != 'n') return false;
    *out = (pitch_class + 12) % 12;
    return true;
}

bool derive_native_superset_ignores(
    const ChordMatch& match,
    const std::set<int>& intended_pitch_classes,
    const std::set<int>& semantic_pitch_classes,
    const NativeAssetCapabilities native_assets,
    std::vector<std::string>* out) {
    if (!out) return false;
    out->clear();
    const NativeChordConstituents* mapped = find_verified_native_chord(match.id, native_assets);
    if (!mapped) return false;
    std::set<int> native_pitch_classes;
    for (std::size_t index = 0; index < mapped->sound_count; ++index) {
        int pitch_class = 0;
        if (!native_sound_pitch_class(mapped->sound_names[index], &pitch_class) ||
            semantic_pitch_classes.count(pitch_class) == 0 ||
            !native_pitch_classes.insert(pitch_class).second) return false;
        if (intended_pitch_classes.count(pitch_class) == 0) {
            out->push_back(std::string(mapped->sound_names[index]));
        }
    }
    // A partial ninth may not rely on the template seventh: native ninth
    // assignments omit it, so that source tone cannot be reproduced or ignored.
    if (!std::includes(native_pitch_classes.begin(), native_pitch_classes.end(),
            intended_pitch_classes.begin(), intended_pitch_classes.end()) || out->size() > 3u) return false;
    return true;
}

struct SupersetChordMatch {
    ChordMatch chord;
    std::vector<std::string> ignored_sounds;
};

SupersetChordMatch infer_unique_native_chord_superset(
    const std::set<int>& intended_pitch_classes,
    const std::vector<const MidiNoteEvent*>& harmony,
    const bool flat_db_major,
    const NativeAssetCapabilities native_assets) {
    // Dyads, doubled pitch classes, and cross-track/channel clusters are too
    // under-specified to establish one intended source voicing safely.
    if (intended_pitch_classes.size() < 3u || harmony.size() != intended_pitch_classes.size()) return {};
    const int source_track = harmony.front()->source.track;
    const int source_channel = harmony.front()->source.channel;
    if (std::any_of(harmony.begin(), harmony.end(), [&](const MidiNoteEvent* note) {
            return note->source.track != source_track || note->source.channel != source_channel;
        })) return {};
    const MidiNoteEvent* bass = *std::min_element(harmony.begin(), harmony.end(),
        [](const MidiNoteEvent* left, const MidiNoteEvent* right) {
            if (left->source.pitch != right->source.pitch) return left->source.pitch < right->source.pitch;
            return left->source < right->source;
        });
    const int bass_pitch_class = bass->source.pitch % 12;
    std::vector<SupersetChordMatch> candidates;
    std::size_t semantic_candidate_count = 0;
    for (int root = 0; root < 12; ++root) {
        if (root != bass_pitch_class) continue;
        for (const ChordTemplate& chord : kChordTemplates) {
            const char* id = native_chord_id(root, chord.quality, flat_db_major, native_assets);
            if (!id) continue;
            std::set<int> expected;
            for (std::size_t index = 0; index < chord.size; ++index) {
                expected.insert((root + chord.intervals[index]) % 12);
            }
            if (expected == intended_pitch_classes ||
                !std::includes(expected.begin(), expected.end(),
                    intended_pitch_classes.begin(), intended_pitch_classes.end())) continue;
            // Ambiguity is decided from the musical templates before consulting
            // native voicing omissions (notably the omitted seventh in ninths).
            if (++semantic_candidate_count != 1u) return {};
            SupersetChordMatch candidate;
            candidate.chord = {id, root, chord.quality};
            if (!derive_native_superset_ignores(candidate.chord, intended_pitch_classes,
                    expected, native_assets, &candidate.ignored_sounds)) continue;
            candidates.push_back(std::move(candidate));
        }
    }
    if (candidates.size() != 1u) return {};
    return std::move(candidates.front());
}

// Half-open source-tick intervals: an ended note (including this exact tick)
// grants no context. Ordered events/queries avoid scanning all melody attacks
// for every accompaniment onset. Nothing from a future attack is consulted.
std::map<int, int> sounding_melody_floors(
    const std::vector<OnsetCluster>& clusters, const std::set<SourceIdentity>& melody_sources) {
    struct Change { int tick; int pitch; int delta; };
    std::vector<Change> changes;
    for (const auto& source : melody_sources) {
        if (source.end_tick <= source.tick) continue;
        changes.push_back({source.tick, source.pitch, 1});
        changes.push_back({source.end_tick, source.pitch, -1});
    }
    std::sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) {
        return std::tie(a.tick, a.delta, a.pitch) < std::tie(b.tick, b.delta, b.pitch);
    });
    std::map<int, int> floors;
    for (const auto& cluster : clusters)
        for (const auto& note : cluster.notes) floors.emplace(note.source.tick, 128);
    std::multiset<int> sounding;
    std::size_t next = 0;
    for (auto& [tick, floor] : floors) {
        while (next < changes.size() && changes[next].tick <= tick) {
            const auto& change = changes[next++];
            if (change.delta > 0) sounding.insert(change.pitch);
            else sounding.erase(sounding.find(change.pitch));
        }
        if (!sounding.empty()) floor = *sounding.begin();
    }
    return floors;
}

bool build_partial_chord_candidate(
    const OnsetCluster& cluster, const std::vector<const MidiNoteEvent*>& harmony,
    const std::set<int>& fresh_pitch_classes, const std::set<SourceIdentity>& melody_sources,
    const std::map<int, int>& sounding_floors, const NativeAssetCapabilities native_assets,
    Attack* out) {
    if (fresh_pitch_classes.empty() || fresh_pitch_classes.size() > 2u ||
        !native_assets.has_verified_authored_chord_voicing()) return false;
    int same_onset_floor = 128;
    for (const auto& note : cluster.notes)
        if (melody_sources.count(note.source))
            same_onset_floor = std::min(same_onset_floor, note.source.pitch);
    std::set<int> supported_pitches;
    for (const auto& carrier : kVerifiedNativeChordConstituents) {
        if (!find_verified_native_chord(carrier.chord_id, native_assets)) continue;
        for (std::size_t index = 0; index < carrier.sound_count; ++index) {
            const auto sound = carrier.sound_names[index];
            int pitch_class = 0;
            if (native_sound_pitch_class(sound, &pitch_class))
                supported_pitches.insert((sound[2] - '0' + 1) * 12 + pitch_class);
        }
    }
    // Fix the intended fresh notes before choosing a carrier. Sustained melody
    // is context only, never a chord constituent or a newly emitted attack.
    // Only octave doubling may be reduced; no unmappable class is discarded.
    std::vector<const MidiNoteEvent*> intended;
    for (const int pitch_class : fresh_pitch_classes) {
        const MidiNoteEvent* source = nullptr;
        for (const auto* note : harmony) {
            const int floor = same_onset_floor != 128 ? same_onset_floor :
                sounding_floors.at(note->source.tick);
            if (floor != 128 && note->source.pitch % 12 == pitch_class &&
                note->source.pitch < floor && supported_pitches.count(note->source.pitch) &&
                (!source || note->source.pitch < source->source.pitch ||
                    (note->source.pitch == source->source.pitch && note->source < source->source)))
                source = note;
        }
        if (!source) return false;
        intended.push_back(source);
    }
    if (std::any_of(intended.begin(), intended.end(), [&](const auto* note) {
        return note->source.tick != intended.front()->source.tick ||
            note->source.track != intended.front()->source.track ||
            note->source.channel != intended.front()->source.channel;
    })) return false;
    std::string best_id;
    std::vector<std::string> best_ignores;
    for (const auto& carrier : kVerifiedNativeChordConstituents) {
        if (!find_verified_native_chord(carrier.chord_id, native_assets)) continue;
        std::size_t matched = 0;
        std::vector<std::string> ignores;
        for (std::size_t index = 0; index < carrier.sound_count; ++index) {
            const auto sound = carrier.sound_names[index];
            int pitch_class = 0;
            if (!native_sound_pitch_class(sound, &pitch_class)) continue;
            const int pitch = (sound[2] - '0' + 1) * 12 + pitch_class;
            if (std::any_of(intended.begin(), intended.end(), [&](const auto* note) {
                return note->source.pitch == pitch;
            })) ++matched;
            else ignores.emplace_back(sound);
        }
        if (matched != intended.size() || ignores.size() > 3u) continue;
        if (best_id.empty() || ignores.size() < best_ignores.size() ||
            (ignores.size() == best_ignores.size() && carrier.chord_id < best_id)) {
            best_id = carrier.chord_id;
            best_ignores = std::move(ignores);
        }
    }
    if (best_id.empty()) return false;
    const auto* source = *std::min_element(intended.begin(), intended.end(),
        [](const auto* a, const auto* b) { return a->source < b->source; });
    out->event = *source;
    out->start = source->start;
    out->end = source->end;
    out->beat = source->beat;
    out->metric_accent = cluster.metric_accent;
    out->melody_evidence = midi_melody_scoring_detail::melody_evidence(
        source->source.pitch, source->source.velocity, source->start, source->end, source->stream_prior);
    out->chord_id = std::move(best_id);
    out->ignore_sound_pitches = std::move(best_ignores);
    std::sort(intended.begin(), intended.end(), [](const auto* a, const auto* b) {
        if (a->source.pitch != b->source.pitch) return a->source.pitch < b->source.pitch;
        return a->source < b->source;
    });
    for (const auto* note : intended) {
        out->end = std::max(out->end, note->end);
        out->source_chord_pitches.push_back(pitch_name(note->source.pitch));
        out->chord_sources.push_back(note->source);
    }
    return true;
}

std::vector<Attack> build_chord_candidates(
    const std::vector<OnsetCluster>& clusters,
    const std::set<SourceIdentity>& melody_sources,
    const std::vector<MidiAccidentalOrientationChange>& accidental_orientation,
    const NativeAssetCapabilities native_assets) {
    std::vector<Attack> result;
    const auto sounding_floors = sounding_melody_floors(clusters, melody_sources);
    for (const OnsetCluster& cluster : clusters) {
        std::vector<const MidiNoteEvent*> harmony;
        std::set<int> fresh_pitch_classes;
        for (const MidiNoteEvent& note : cluster.notes) {
            if (melody_sources.find(note.source) != melody_sources.end()) continue;
            harmony.push_back(&note);
            fresh_pitch_classes.insert(note.source.pitch % 12);
        }
        const MidiAccidentalOrientationChange* shared_flat_context = nullptr;
        bool flat_db_major = !harmony.empty();
        for (const MidiNoteEvent* note : harmony) {
            const MidiAccidentalOrientationChange* context = midi_accidental_context_at_tick(
                accidental_orientation, note->source.tick);
            if (!context || context->orientation != MidiAccidentalOrientation::Flat ||
                (shared_flat_context && shared_flat_context->tick != context->tick)) {
                flat_db_major = false;
                break;
            }
            shared_flat_context = context;
        }
        ChordMatch match = infer_native_chord_match(
            fresh_pitch_classes, flat_db_major, native_assets);
        std::vector<std::string> ignored_sounds;
        if (match.id.empty()) {
            SupersetChordMatch superset = infer_unique_native_chord_superset(
                fresh_pitch_classes, harmony, flat_db_major, native_assets);
            match = std::move(superset.chord);
            ignored_sounds = std::move(superset.ignored_sounds);
        }
        if (match.id.empty()) {
            Attack partial;
            if (build_partial_chord_candidate(cluster, harmony, fresh_pitch_classes,
                    melody_sources, sounding_floors, native_assets, &partial))
                result.push_back(std::move(partial));
            continue;
        }
        if (match.quality == ChordQuality::Diminished) {
            const int possible_dominant_root = (match.root + 8) % 12;
            const bool inversion_ambiguity = std::any_of(cluster.notes.begin(), cluster.notes.end(),
                [&](const MidiNoteEvent& note) {
                    return melody_sources.find(note.source) != melody_sources.end() &&
                        note.source.pitch % 12 == possible_dominant_root;
                });
            if (inversion_ambiguity) continue;
        }
        const MidiNoteEvent* root_note = nullptr;
        double maximum_end = cluster.start;
        for (const MidiNoteEvent* note : harmony) {
            maximum_end = std::max(maximum_end, note->end);
            if (note->source.pitch % 12 == match.root &&
                (!root_note || note->source.velocity > root_note->source.velocity ||
                    (note->source.velocity == root_note->source.velocity && note->source < root_note->source))) {
                root_note = note;
            }
        }
        if (!root_note) continue;
        Attack chord;
        chord.event = *root_note;
        chord.start = cluster.start;
        chord.end = maximum_end;
        chord.beat = cluster.beat;
        chord.metric_accent = cluster.metric_accent;
        chord.melody_evidence = midi_melody_scoring_detail::melody_evidence(
            root_note->source.pitch,
            root_note->source.velocity,
            root_note->start,
            root_note->end,
            root_note->stream_prior);
        chord.chord_id = match.id;
        chord.ignore_sound_pitches = std::move(ignored_sounds);
        std::sort(harmony.begin(), harmony.end(), [](const MidiNoteEvent* a, const MidiNoteEvent* b) {
            if (a->source.pitch != b->source.pitch) return a->source.pitch < b->source.pitch;
            return a->source < b->source;
        });
        for (const MidiNoteEvent* note : harmony) {
            chord.source_chord_pitches.push_back(pitch_name(note->source.pitch));
            chord.chord_sources.push_back(note->source);
        }
        result.push_back(std::move(chord));
    }
    return result;
}

std::vector<Attack> build_fallback_candidates(
    const std::vector<OnsetCluster>& clusters,
    const std::vector<Attack>& voice,
    const std::set<SourceIdentity>& melody_sources) {
    std::vector<Attack> candidates;
    for (const Attack& attack : voice) {
        if (attack.melody_evidence < kPrimaryVoiceEvidence) {
            Attack fallback = attack;
            fallback.fallback = true;
            candidates.push_back(std::move(fallback));
        }
    }
    for (const OnsetCluster& cluster : clusters) {
        const MidiNoteEvent* best = nullptr;
        for (const MidiNoteEvent& note : cluster.notes) {
            if (melody_sources.find(note.source) != melody_sources.end()) continue;
            if (!best || midi_melody_scoring_detail::voice_emission(
                    note.source.pitch, note.source.velocity, note.start, note.end,
                    note.stream_prior, cluster.metric_accent) >
                    midi_melody_scoring_detail::voice_emission(
                        best->source.pitch, best->source.velocity, best->start, best->end,
                        best->stream_prior, cluster.metric_accent) + kComparisonEpsilon ||
                (std::fabs(midi_melody_scoring_detail::voice_emission(
                        note.source.pitch, note.source.velocity, note.start, note.end,
                        note.stream_prior, cluster.metric_accent) -
                    midi_melody_scoring_detail::voice_emission(
                        best->source.pitch, best->source.velocity, best->start, best->end,
                        best->stream_prior, cluster.metric_accent)) <= kComparisonEpsilon &&
                    note.source < best->source)) {
                best = &note;
            }
        }
        if (!best) continue;
        Attack fallback;
        fallback.event = *best;
        fallback.start = best->start;
        fallback.end = best->end;
        fallback.beat = best->beat;
        fallback.metric_accent = cluster.metric_accent;
        fallback.melody_evidence = midi_melody_scoring_detail::melody_evidence(
            best->source.pitch,
            best->source.velocity,
            best->start,
            best->end,
            best->stream_prior);
        fallback.fallback = true;
        candidates.push_back(std::move(fallback));
    }
    std::sort(candidates.begin(), candidates.end(), attack_less);
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const Attack& a, const Attack& b) {
        return a.event.source == b.event.source;
    }), candidates.end());
    // Preliminary melody attacks may disappear at timing/reduction gates. Keep
    // one source-backed alternate per cluster until actual selection; the existing
    // fallback evidence/score penalties prefer the tracked melody, not arbitrary
    // accompaniment filling. Frame collisions and spacing resolve competition.
    return candidates;
}

struct OutputRow {
    Note note;
    bool has_right = false;
    bool has_left = false;
    Attack right;
    Attack left;
    bool protected_action = false;
};

struct OutputAction {
    long long desired_frame = 0;
    bool right = false;
    bool final_right = false;
    Attack attack;
};

struct AnalysisNote {
    double beat = 0.0;
    int pitch = -1;
    std::string_view chord_id;
    bool right_action = false;
    bool left_action = false;
};

struct JointStrainAnalysis {
    MidiJointStrainMetrics metrics;
    std::vector<double> samples;
};

int strain_pitch_number(const std::string& pitch) {
    if (pitch.size() < 2) return -1;
    static constexpr std::array<int, 7> natural_pitch_classes{{9, 11, 0, 2, 4, 5, 7}};
    if (pitch[0] < 'A' || pitch[0] > 'G') return -1;
    int pitch_class = natural_pitch_classes[static_cast<std::size_t>(pitch[0] - 'A')];
    std::size_t octave_index = 1;
    if (pitch[1] == '#' || pitch[1] == 's') {
        ++pitch_class;
        octave_index = 2;
    } else if (pitch[1] == 'b') {
        --pitch_class;
        octave_index = 2;
    } else if (pitch[1] == 'n') {
        octave_index = 2;
    }
    if (octave_index >= pitch.size()) return -1;
    int octave = 0;
    for (std::size_t index = octave_index; index < pitch.size(); ++index) {
        if (pitch[index] < '0' || pitch[index] > '9') return -1;
        octave = octave * 10 + pitch[index] - '0';
    }
    pitch_class = (pitch_class + 12) % 12;
    const int midi_pitch = (octave + 1) * 12 + pitch_class;
    return midi_pitch >= 0 && midi_pitch <= 127 ? midi_pitch : -1;
}

std::vector<AnalysisNote> analysis_notes_from_notes(const std::vector<Note>& notes) {
    std::vector<AnalysisNote> result;
    result.reserve(notes.size());
    std::uint8_t previous_group = 0;
    for (const Note& note : notes) {
        const bool continuation = note.group_index != 0
            && note.group_index == previous_group;
        result.push_back({note.beat, strain_pitch_number(note.pitch), note.chord_id,
            !note.pitch.empty() && !continuation,
            !note.chord_id.empty() && !continuation});
        previous_group = note.group_index;
    }
    return result;
}

JointStrainAnalysis calculate_joint_strain_analysis(
    const std::vector<AnalysisNote>& notes, const double bpm, const bool notes_are_sorted = false) {
    JointStrainAnalysis result;
    if (notes.empty() || bpm <= 0.0) return result;
    std::vector<AnalysisNote> sorted_notes;
    const std::vector<AnalysisNote>* ordered_notes = &notes;
    if (!notes_are_sorted) {
        sorted_notes = notes;
        std::stable_sort(sorted_notes.begin(), sorted_notes.end(), [](const AnalysisNote& a, const AnalysisNote& b) {
            return a.beat < b.beat;
        });
        ordered_notes = &sorted_notes;
    }
    constexpr double kOneSecondDecayBase = 0.15;
    double strain = 0.0;
    double previous_seconds = 0.0;
    int previous_pitch = -1;
    int previous_direction = 0;
    int previous_hand_mask = 0;
    std::string_view previous_chord;
    bool has_previous = false;
    result.samples.reserve(ordered_notes->size());
    for (const AnalysisNote& note : *ordered_notes) {
        if (!note.right_action && !note.left_action) continue;
        const double seconds = note.beat * 60.0 / bpm;
        const double elapsed = has_previous ? std::max(0.0, seconds - previous_seconds) : 10.0;
        const bool connected_transition = has_previous && elapsed < 2.0;
        const int pitch = note.pitch;
        const bool has_pitch = note.right_action && pitch >= 0;
        const bool has_chord = note.left_action;
        const int hand_mask = (has_pitch ? 1 : 0) | (has_chord ? 2 : 0);
        double impulse = 1.0;
        if (has_chord) impulse += 0.20;
        if (hand_mask == 3) impulse += 0.20;
        if (connected_transition && hand_mask != previous_hand_mask) impulse += 0.08;
        if (connected_transition && has_chord && !previous_chord.empty() &&
            note.chord_id != previous_chord) {
            impulse += 0.08;
        }
        if (has_previous && has_pitch && previous_pitch >= 0) {
            const int delta = pitch - previous_pitch;
            const int movement = std::abs(delta);
            const int direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
            impulse += 0.50 * std::min(1.0, static_cast<double>(movement) / 12.0);
            if (movement >= 12) impulse += 0.20;
            if (connected_transition && movement >= 3 && direction != 0 && previous_direction != 0 &&
                direction != previous_direction) {
                impulse += 0.12;
            }
            if (direction != 0) previous_direction = direction;
        }
        thread_local std::unordered_map<double, double> decay_cache;
        const auto decay = decay_cache.find(elapsed);
        const double decay_factor = decay != decay_cache.end() ? decay->second :
            decay_cache.emplace(elapsed, std::pow(kOneSecondDecayBase, elapsed)).first->second;
        strain = strain * decay_factor + impulse;
        result.samples.push_back(strain);
        result.metrics.peak = std::max(result.metrics.peak, strain);
        previous_seconds = seconds;
        if (has_pitch) previous_pitch = pitch;
        previous_hand_mask = hand_mask;
        if (has_chord) previous_chord = note.chord_id;
        has_previous = true;
    }
    const double position = 0.95 * static_cast<double>(result.samples.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1, result.samples.size() - 1);
    std::nth_element(result.samples.begin(), result.samples.begin() + lower, result.samples.end());
    const double lower_value = result.samples[lower];
    const double upper_value = upper == lower ? lower_value :
        *std::min_element(result.samples.begin() + static_cast<std::ptrdiff_t>(lower + 1), result.samples.end());
    result.metrics.p95 = lower_value + (upper_value - lower_value) *
        (position - static_cast<double>(lower));
    return result;
}

JointStrainAnalysis calculate_joint_strain(
    const std::vector<Note>& notes, const double bpm, const bool notes_are_sorted = false) {
    return calculate_joint_strain_analysis(analysis_notes_from_notes(notes), bpm, notes_are_sorted);
}

struct SkillAction {
    double seconds = 0.0;
    bool right = false;
    int pitch = -1;
    std::string_view id;
};

double interpolated_quantile(std::vector<double>& values, const double fraction) {
    if (values.empty()) return 0.0;
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1, values.size() - 1);
    std::nth_element(values.begin(), values.begin() + lower, values.end());
    const double lower_value = values[lower];
    const double upper_value = upper == lower ? lower_value :
        *std::min_element(values.begin() + static_cast<std::ptrdiff_t>(lower + 1), values.end());
    return lower_value + (upper_value - lower_value) *
        (position - static_cast<double>(lower));
}

MidiLocalSkillMetrics calculate_local_skills_analysis(
    const std::vector<AnalysisNote>& notes, const double bpm, const bool notes_are_sorted = false,
    const bool monotonic_only = false) {
    MidiLocalSkillMetrics result;
    if (bpm <= 0.0) return result;
    std::vector<SkillAction> actions;
    actions.reserve(notes.size() * 2);
    for (const AnalysisNote& note : notes) {
        const double seconds = note.beat * 60.0 / bpm;
        if (note.right_action) {
            actions.push_back({seconds, true, note.pitch, {}});
        }
        if (note.left_action) actions.push_back({seconds, false, -1, note.chord_id});
    }
    if (!notes_are_sorted) {
        std::stable_sort(actions.begin(), actions.end(), [](const SkillAction& a, const SkillAction& b) {
            if (std::fabs(a.seconds - b.seconds) > kComparisonEpsilon) return a.seconds < b.seconds;
            if (a.right != b.right) return a.right;
            return a.id < b.id;
        });
    }
    if (actions.empty()) return result;

    for (std::size_t window_index = 0; window_index < kMidiSkillWindowSeconds.size(); ++window_index) {
        const double window = kMidiSkillWindowSeconds[window_index];
        for (std::size_t begin = 0, end = 0; begin < actions.size(); ++begin) {
            end = std::max(end, begin);
            while (end < actions.size() &&
                   actions[end].seconds < actions[begin].seconds + window - kComparisonEpsilon) ++end;
            if (end - begin > result.maximum_window_actions[window_index]) {
                result.maximum_window_actions[window_index] = end - begin;
                result.maximum_window_begin_seconds[window_index] = actions[begin].seconds;
            }
        }
    }

    const auto measure_stream = [&](const double maximum_gap, std::size_t* maximum_actions,
                                    double* maximum_duration, double* actions_begin,
                                    double* actions_end, double* duration_begin, double* duration_end) {
        std::size_t begin = 0;
        for (std::size_t end = 1; end <= actions.size(); ++end) {
            if (end < actions.size() &&
                actions[end].seconds - actions[end - 1].seconds <= maximum_gap + kComparisonEpsilon) {
                continue;
            }
            const std::size_t count = end - begin;
            const double duration = count > 1 ? actions[end - 1].seconds - actions[begin].seconds : 0.0;
            if (count > *maximum_actions) {
                *maximum_actions = count;
                *actions_begin = actions[begin].seconds;
                *actions_end = actions[end - 1].seconds;
            }
            if (duration > *maximum_duration) {
                *maximum_duration = duration;
                *duration_begin = actions[begin].seconds;
                *duration_end = actions[end - 1].seconds;
            }
            begin = end;
        }
    };
    measure_stream(0.25, &result.maximum_quarter_second_stream_actions,
        &result.maximum_quarter_second_stream_duration,
        &result.maximum_quarter_second_stream_begin_seconds,
        &result.maximum_quarter_second_stream_actions_end_seconds,
        &result.maximum_quarter_second_stream_duration_begin_seconds,
        &result.maximum_quarter_second_stream_duration_end_seconds);
    measure_stream(0.50, &result.maximum_half_second_stream_actions,
        &result.maximum_half_second_stream_duration,
        &result.maximum_half_second_stream_begin_seconds,
        &result.maximum_half_second_stream_actions_end_seconds,
        &result.maximum_half_second_stream_duration_begin_seconds,
        &result.maximum_half_second_stream_duration_end_seconds);

    std::size_t jack_begin = 0;
    std::size_t jack_count = 0;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index > 0 && actions[index].right == actions[index - 1].right &&
            (actions[index].right ? actions[index].pitch == actions[index - 1].pitch :
                actions[index].id == actions[index - 1].id) &&
            actions[index].seconds - actions[index - 1].seconds <= 0.5 + kComparisonEpsilon) {
            ++jack_count;
        } else {
            jack_begin = index;
            jack_count = 1;
        }
        if (jack_count > result.maximum_jack_run) {
            result.maximum_jack_run = jack_count;
            result.maximum_jack_begin_seconds = actions[jack_begin].seconds;
        }
    }

    std::vector<double> rapid_movements;
    std::vector<std::pair<double, bool>> octave_movements;
    std::vector<std::pair<double, bool>> large_reversals;
    if (!monotonic_only) rapid_movements.reserve(actions.size());
    octave_movements.reserve(actions.size());
    large_reversals.reserve(actions.size());
    int previous_direction = 0;
    std::size_t reversal_run = 0;
    std::size_t reversal_begin = 0;
    std::size_t right_actions = 0;
    std::size_t left_actions = 0;
    std::size_t pitch_transitions = 0;
    std::size_t octave_transitions = 0;
    std::size_t previous_right = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (!monotonic_only) {
            right_actions += actions[index].right ? 1u : 0u;
            left_actions += actions[index].right ? 0u : 1u;
        }
        if (!actions[index].right || actions[index].pitch < 0) continue;
        if (previous_right != std::numeric_limits<std::size_t>::max()) {
            const SkillAction& previous = actions[previous_right];
            const double gap = actions[index].seconds - previous.seconds;
            const int delta = actions[index].pitch - previous.pitch;
            const int movement = std::abs(delta);
            const int direction = delta > 0 ? 1 : delta < 0 ? -1 : 0;
            if (gap <= 0.75 + kComparisonEpsilon) {
                if (!monotonic_only) rapid_movements.push_back(static_cast<double>(movement));
                if (movement > result.rapid_movement_maximum) {
                    result.rapid_movement_maximum = static_cast<double>(movement);
                    result.rapid_movement_maximum_seconds = actions[index].seconds;
                }
                if (!monotonic_only) ++pitch_transitions;
                if (movement >= 12) {
                    if (!monotonic_only) ++octave_transitions;
                    octave_movements.emplace_back(actions[index].seconds, true);
                }
                const bool reversal = gap <= 0.5 + kComparisonEpsilon && movement >= 3 &&
                    direction != 0 && previous_direction != 0 && direction != previous_direction;
                if (reversal) {
                    if (reversal_run == 0) reversal_begin = previous_right;
                    ++reversal_run;
                    if (movement >= 12) large_reversals.emplace_back(actions[index].seconds, true);
                } else {
                    reversal_run = 0;
                }
                if (reversal_run + 1 > result.maximum_reversal_run) {
                    result.maximum_reversal_run = reversal_run + 1;
                    result.maximum_reversal_begin_seconds = actions[reversal_begin].seconds;
                }
            } else {
                reversal_run = 0;
            }
            if (direction != 0) previous_direction = direction;
        }
        previous_right = index;
    }
    if (!monotonic_only) {
        result.rapid_movement_p90 = interpolated_quantile(rapid_movements, 0.90);
        result.octave_movement_rate = pitch_transitions == 0 ? 0.0 :
            static_cast<double>(octave_transitions) / static_cast<double>(pitch_transitions);
    }
    const auto maximum_event_window = [](const std::vector<std::pair<double, bool>>& events,
                                         const double window, double* begin_seconds) {
        std::size_t maximum = 0;
        for (std::size_t begin = 0, end = 0; begin < events.size(); ++begin) {
            end = std::max(end, begin);
            while (end < events.size() && events[end].first < events[begin].first + window - kComparisonEpsilon) ++end;
            if (end - begin > maximum) {
                maximum = end - begin;
                *begin_seconds = events[begin].first;
            }
        }
        return maximum;
    };
    result.maximum_octave_movements_in_five_seconds = maximum_event_window(
        octave_movements, 5.0, &result.maximum_octave_window_begin_seconds);
    result.maximum_large_reversals_in_five_seconds = maximum_event_window(
        large_reversals, 5.0, &result.maximum_large_reversal_window_begin_seconds);

    struct FatigueState {
        double immediate = 0.0;
        double slow = 0.0;
        double last_seconds = -100.0;
    };
    FatigueState right_fatigue;
    FatigueState left_fatigue;
    thread_local std::unordered_map<double, double> immediate_decay_cache;
    thread_local std::unordered_map<double, double> slow_decay_cache;
    for (const SkillAction& action : actions) {
        FatigueState& state = action.right ? right_fatigue : left_fatigue;
        const double elapsed = std::max(0.0, action.seconds - state.last_seconds);
        const auto immediate_decay = immediate_decay_cache.find(elapsed);
        const double immediate_factor = immediate_decay != immediate_decay_cache.end() ? immediate_decay->second :
            immediate_decay_cache.emplace(elapsed, std::exp(-elapsed / 0.80)).first->second;
        const auto slow_decay = slow_decay_cache.find(elapsed);
        const double slow_factor = slow_decay != slow_decay_cache.end() ? slow_decay->second :
            slow_decay_cache.emplace(elapsed, std::exp(-elapsed / 6.0)).first->second;
        state.immediate = state.immediate * immediate_factor + 1.0;
        state.slow = state.slow * slow_factor + 0.35;
        state.last_seconds = action.seconds;
        const double fatigue = state.immediate + state.slow;
        double& peak = action.right ? result.right_fatigue_peak : result.left_fatigue_peak;
        double& peak_seconds = action.right ? result.right_fatigue_peak_seconds : result.left_fatigue_peak_seconds;
        if (fatigue > peak) {
            peak = fatigue;
            peak_seconds = action.seconds;
        }
    }
    if (!monotonic_only) {
        result.hand_imbalance = actions.empty() ? 0.0 :
            std::fabs(static_cast<double>(right_actions) - static_cast<double>(left_actions)) /
                static_cast<double>(actions.size());
    }

    std::vector<double> irregularities;
    irregularities.reserve(actions.size());
    double previous_gap = 0.0;
    for (std::size_t index = 1; index < actions.size(); ++index) {
        const double gap = actions[index].seconds - actions[index - 1].seconds;
        if (previous_gap > 1.0 / 60.0 - kComparisonEpsilon && gap > 1.0 / 60.0 - kComparisonEpsilon &&
            previous_gap <= 1.0 + kComparisonEpsilon && gap <= 1.0 + kComparisonEpsilon) {
            const double ratio = std::max(gap, previous_gap) / std::min(gap, previous_gap);
            if (!monotonic_only) irregularities.push_back(ratio);
            if (ratio > result.rhythm_irregularity_maximum) {
                result.rhythm_irregularity_maximum = ratio;
                result.rhythm_irregularity_peak_seconds = actions[index].seconds;
            }
        }
        previous_gap = gap;
    }
    if (!monotonic_only) {
        result.rhythm_irregularity_p90 = interpolated_quantile(irregularities, 0.90);
    }
    return result;
}

MidiLocalSkillMetrics calculate_local_skills(
    const std::vector<Note>& notes, const double bpm, const bool notes_are_sorted = false,
    const bool monotonic_only = false) {
    return calculate_local_skills_analysis(
        analysis_notes_from_notes(notes), bpm, notes_are_sorted, monotonic_only);
}

double local_skill_violation(
    const MidiLocalSkillMetrics& metrics,
    const Profile::SkillRoute& route,
    int* dominant_skill = nullptr,
    double* hardest_begin = nullptr,
    double* hardest_end = nullptr,
    bool* dominant_is_global = nullptr,
    const bool monotonic_only = false) {
    double maximum = 0.0;
    const auto consider = [&](const double ratio, const int skill, const double begin,
                              const double end, const bool global = false) {
        if (ratio > maximum) {
            maximum = ratio;
            if (dominant_skill) *dominant_skill = skill;
            if (hardest_begin) *hardest_begin = global ? -1.0 : begin;
            if (hardest_end) *hardest_end = global ? -1.0 : end;
            if (dominant_is_global) *dominant_is_global = global;
        }
    };
    for (std::size_t index = 0; index < route.maximum_window_actions.size(); ++index) {
        consider(static_cast<double>(metrics.maximum_window_actions[index]) /
                std::max<std::size_t>(1, route.maximum_window_actions[index]),
            1, metrics.maximum_window_begin_seconds[index],
            metrics.maximum_window_begin_seconds[index] + kMidiSkillWindowSeconds[index]);
    }
    consider(static_cast<double>(metrics.maximum_quarter_second_stream_actions) /
            std::max<std::size_t>(1, route.maximum_quarter_second_stream_actions),
        2, metrics.maximum_quarter_second_stream_begin_seconds,
        metrics.maximum_quarter_second_stream_actions_end_seconds);
    consider(metrics.maximum_quarter_second_stream_duration /
            std::max(0.01, route.maximum_quarter_second_stream_duration),
        2, metrics.maximum_quarter_second_stream_duration_begin_seconds,
        metrics.maximum_quarter_second_stream_duration_end_seconds);
    consider(static_cast<double>(metrics.maximum_half_second_stream_actions) /
            std::max<std::size_t>(1, route.maximum_half_second_stream_actions),
        2, metrics.maximum_half_second_stream_begin_seconds,
        metrics.maximum_half_second_stream_actions_end_seconds);
    consider(metrics.maximum_half_second_stream_duration /
            std::max(0.01, route.maximum_half_second_stream_duration),
        2, metrics.maximum_half_second_stream_duration_begin_seconds,
        metrics.maximum_half_second_stream_duration_end_seconds);
    consider(static_cast<double>(metrics.maximum_jack_run) /
            std::max<std::size_t>(1, route.maximum_jack_run),
        3, metrics.maximum_jack_begin_seconds,
        metrics.maximum_jack_begin_seconds + 0.5 * metrics.maximum_jack_run);
    consider(static_cast<double>(metrics.maximum_reversal_run) /
            std::max<std::size_t>(1, route.maximum_reversal_run),
        4, metrics.maximum_reversal_begin_seconds,
        metrics.maximum_reversal_begin_seconds + 0.5 * metrics.maximum_reversal_run);
    if (!monotonic_only) {
        consider(metrics.rapid_movement_p90 / std::max(1.0, route.maximum_rapid_movement_p90),
            5, -1.0, -1.0, true);
    }
    consider(metrics.rapid_movement_maximum / std::max(1.0, route.maximum_rapid_movement),
        5, metrics.rapid_movement_maximum_seconds, metrics.rapid_movement_maximum_seconds);
    consider(static_cast<double>(metrics.maximum_octave_movements_in_five_seconds) /
            std::max<std::size_t>(1, route.maximum_octave_movements_in_five_seconds),
        6, metrics.maximum_octave_window_begin_seconds, metrics.maximum_octave_window_begin_seconds + 5.0);
    consider(static_cast<double>(metrics.maximum_large_reversals_in_five_seconds) /
            std::max<std::size_t>(1, route.maximum_large_reversals_in_five_seconds),
        6, metrics.maximum_large_reversal_window_begin_seconds,
        metrics.maximum_large_reversal_window_begin_seconds + 5.0);
    if (!monotonic_only) {
        consider(metrics.octave_movement_rate / std::max(0.001, route.maximum_octave_movement_rate),
            6, -1.0, -1.0, true);
    }
    consider(metrics.right_fatigue_peak / std::max(0.1, route.maximum_right_fatigue),
        7, metrics.right_fatigue_peak_seconds, metrics.right_fatigue_peak_seconds);
    consider(metrics.left_fatigue_peak / std::max(0.1, route.maximum_left_fatigue),
        7, metrics.left_fatigue_peak_seconds, metrics.left_fatigue_peak_seconds);
    if (!monotonic_only) {
        consider(metrics.hand_imbalance / std::max(0.01, route.maximum_hand_imbalance),
            7, -1.0, -1.0, true);
        consider(metrics.rhythm_irregularity_p90 /
                std::max(1.0, route.maximum_rhythm_irregularity_p90),
            8, -1.0, -1.0, true);
    }
    consider(metrics.rhythm_irregularity_maximum /
            std::max(1.0, route.maximum_rhythm_irregularity),
        8, metrics.rhythm_irregularity_peak_seconds, metrics.rhythm_irregularity_peak_seconds);
    return maximum;
}

double best_route_violation(const MidiLocalSkillMetrics& metrics, const Profile& profile,
                            int* route_index = nullptr, int* dominant_skill = nullptr,
                            double* hardest_begin = nullptr, double* hardest_end = nullptr,
                            bool* dominant_is_global = nullptr, double* raw_ratio = nullptr,
                            double* route_margin = nullptr) {
    double best = std::numeric_limits<double>::infinity();
    double best_margin = std::numeric_limits<double>::infinity();
    bool best_is_feasible = false;
    for (std::size_t index = 0; index < profile.route_count; ++index) {
        int skill = 0;
        double begin = 0.0;
        double end = 0.0;
        bool global = false;
        const double violation = local_skill_violation(
            metrics, profile.routes[index], &skill, &begin, &end, &global);
        const double margin = profile.routes[index].evidence_margin;
        const double normalized = violation / margin;
        const bool feasible = violation <= margin + kComparisonEpsilon;
        const bool stronger_feasible_route = feasible && (!best_is_feasible ||
            margin < best_margin - kComparisonEpsilon);
        const bool better_same_class = feasible == best_is_feasible &&
            std::fabs(margin - best_margin) <= kComparisonEpsilon && normalized < best;
        const bool better_near_route = !feasible && !best_is_feasible && normalized < best;
        if (stronger_feasible_route || better_same_class || better_near_route) {
            best = normalized;
            best_margin = margin;
            best_is_feasible = feasible;
            if (route_index) *route_index = static_cast<int>(index);
            if (dominant_skill) *dominant_skill = skill;
            if (hardest_begin) *hardest_begin = begin;
            if (hardest_end) *hardest_end = end;
            if (dominant_is_global) *dominant_is_global = global;
            if (raw_ratio) *raw_ratio = violation;
            if (route_margin) *route_margin = margin;
        }
    }
    return std::isfinite(best) ? best : 0.0;
}

std::uint32_t admissible_route_mask(const MidiLocalSkillMetrics& metrics, const Profile& profile) {
    std::uint32_t mask = 0;
    for (std::size_t index = 0; index < profile.route_count; ++index) {
        const double lower_bound = local_skill_violation(
            metrics, profile.routes[index], nullptr, nullptr, nullptr, nullptr, true);
        if (lower_bound <= profile.routes[index].evidence_margin + kComparisonEpsilon) {
            mask |= std::uint32_t{1} << index;
        }
    }
    return mask;
}

double best_masked_route_load(
    const MidiLocalSkillMetrics& metrics, const Profile& profile, const std::uint32_t route_mask,
    const bool monotonic_only) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < profile.route_count; ++index) {
        if ((route_mask & (std::uint32_t{1} << index)) == 0) continue;
        const double ratio = local_skill_violation(
            metrics, profile.routes[index], nullptr, nullptr, nullptr, nullptr, monotonic_only);
        best = std::min(best, ratio / profile.routes[index].evidence_margin);
    }
    return best;
}

void annotate_best_route(MidiLocalSkillMetrics* metrics, const Profile& profile) {
    if (!metrics) return;
    int route = -1;
    double ratio = 0.0;
    double margin = 0.0;
    best_route_violation(*metrics, profile, &route, &metrics->dominant_skill,
        &metrics->hardest_window_begin_seconds, &metrics->hardest_window_end_seconds,
        &metrics->dominant_skill_is_global, &ratio, &margin);
    metrics->satisfied_route = route;
    metrics->satisfied_route_ratio = ratio;
    metrics->satisfied_route_margin = margin;
    metrics->satisfied_route_name = route >= 0 ? profile.routes[static_cast<std::size_t>(route)].name : "";
}

double joint_strain_violation(const MidiJointStrainMetrics& metrics, const Profile& profile) {
    return std::max(
        metrics.p95 / profile.maximum_joint_strain_p95,
        metrics.peak / profile.maximum_joint_strain_peak);
}

double output_row_score(const OutputRow& row) {
    return row.has_right ? attack_score(row.right) : attack_score(row.left);
}

double recoverable_route_penalty(
    const std::vector<AnalysisNote>& notes, const double bpm, const Profile::SkillRoute& route,
    const bool notes_are_sorted = false) {
    if (bpm <= 0.0) return 0.0;
    std::vector<SkillAction> actions;
    actions.reserve(notes.size());
    for (const AnalysisNote& note : notes) {
        const double seconds = note.beat * 60.0 / bpm;
        if (note.right_action) {
            actions.push_back({seconds, true, note.pitch, {}});
        }
        if (note.left_action) actions.push_back({seconds, false, -1, note.chord_id});
    }
    if (!notes_are_sorted) {
        std::stable_sort(actions.begin(), actions.end(), [](const SkillAction& a, const SkillAction& b) {
            if (std::fabs(a.seconds - b.seconds) > kComparisonEpsilon) return a.seconds < b.seconds;
            if (a.right != b.right) return a.right;
            return a.id < b.id;
        });
    }
    const double movement_limit = route.maximum_rapid_movement_p90 * route.evidence_margin;
    const double rhythm_limit = route.maximum_rhythm_irregularity_p90 * route.evidence_margin;
    double penalty = 0.0;
    std::size_t previous_right = std::numeric_limits<std::size_t>::max();
    double previous_gap = 0.0;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].right && actions[index].pitch >= 0) {
            if (previous_right != std::numeric_limits<std::size_t>::max()) {
                const auto& previous = actions[previous_right];
                if (actions[index].seconds - previous.seconds <= 0.75 + kComparisonEpsilon) {
                    const double movement = std::abs(actions[index].pitch - previous.pitch);
                    if (movement > movement_limit + kComparisonEpsilon) {
                        penalty += 10.0 + movement - movement_limit;
                    }
                    if (movement >= 12.0) penalty += 2.0;
                }
            }
            previous_right = index;
        }
        if (index == 0) continue;
        const double gap = actions[index].seconds - actions[index - 1].seconds;
        if (previous_gap > 1.0 / 60.0 - kComparisonEpsilon && gap > 1.0 / 60.0 - kComparisonEpsilon &&
            previous_gap <= 1.0 + kComparisonEpsilon && gap <= 1.0 + kComparisonEpsilon) {
            const double irregularity = std::max(gap, previous_gap) / std::min(gap, previous_gap);
            if (irregularity > rhythm_limit + kComparisonEpsilon) {
                penalty += 5.0 + irregularity - rhythm_limit;
            }
        }
        previous_gap = gap;
    }
    return penalty;
}

struct IncrementalCandidate {
    long long frame = 0;
    OutputRow row;
    double salience = 0.0;
    bool final_right = false;
    bool preferred = false;
};

// A beam path only selects immutable rows. Baseline map nodes and the sorted
// candidate vector own them for the entire search; materialize owning output
// only when returning the selected path. Copying a path must not deep-copy each
// note's strings, chord pitches, and source witnesses.
using IncrementalRow = std::pair<long long, const OutputRow*>;
using IncrementalRows = std::vector<IncrementalRow>;

struct IncrementalState {
    std::shared_ptr<const IncrementalRows> rows;
    const IncrementalCandidate* deferred_candidate = nullptr;
    MidiJointStrainMetrics strain;
    MidiLocalSkillMetrics local_skills;
    MidiLocalSkillMetrics finalized_prefix_skills;
    double salience = 0.0;
    std::size_t preferred_actions = 0;
    mutable double largest_internal_gap = 0.0;
    double maximum_target_timing_error = 0.0;
    double recoverable_penalty = 0.0;
    double finalized_route_load = 0.0;
    double full_route_load = 0.0;
    std::uint32_t admissible_routes = 0;
    std::size_t route_focus = 0;
    std::size_t row_count = 0;
    std::size_t finalized_row_count = 0;
    bool has_finalized_prefix = false;
};

struct IncrementalSelection {
    std::size_t beam_width = 0;
    std::size_t processed_frames = 0;
    std::size_t skill_row_visits = 0;
    Status status = Status::ok_status();
    std::map<long long, OutputRow> rows;
    MidiJointStrainMetrics strain;
    MidiLocalSkillMetrics local_skills;
    std::size_t rejected_candidates = 0;
    std::size_t spacing_rejections = 0;
    std::size_t local_skill_rejections = 0;
    std::size_t target_exclusions = 0;
    std::size_t retained_actions = 0;
};

template <typename Rows>
std::vector<Note> ungrouped_notes_from_rows(const Rows& rows) {
    std::vector<Note> notes;
    notes.reserve(rows.size());
    for (const auto& entry : rows) {
        notes.push_back(entry.second.note);
        notes.back().group_index = 0;
    }
    return notes;
}

std::vector<Note> notes_from_rows(const std::map<long long, OutputRow>& rows, const Profile&) {
    return ungrouped_notes_from_rows(rows);
}

std::vector<Note> notes_from_rows(const IncrementalRows& rows, const Profile&) {
    std::vector<Note> notes;
    notes.reserve(rows.size());
    for (const auto& entry : rows) {
        notes.push_back(entry.second->note);
        notes.back().group_index = 0;
    }
    return notes;
}

std::size_t required_action_count(const std::vector<Note>& notes) {
    std::size_t count = 0;
    std::uint8_t previous_group = 0;
    for (const Note& note : notes) {
        const bool continuation = note.group_index != 0 && note.group_index == previous_group;
        if (!continuation) count += static_cast<std::size_t>(!note.pitch.empty())
            + static_cast<std::size_t>(!note.chord_id.empty());
        previous_group = note.group_index;
    }
    return count;
}

struct InsertionSemantics {
    bool permitted = false;
    std::size_t action_count = 0;
};

InsertionSemantics analyze_incremental_insertion(
    const IncrementalRows& rows,
    const IncrementalCandidate& candidate,
    const Profile& profile,
    const std::size_t current_action_count) {
    InsertionSemantics result;
    const auto insertion = std::lower_bound(rows.begin(), rows.end(), candidate.frame,
        [](const IncrementalRow& row, const long long frame) { return row.first < frame; });
    if (insertion != rows.end() && insertion->first == candidate.frame) return result;
    const std::size_t position = static_cast<std::size_t>(std::distance(rows.begin(), insertion));
    const bool right = candidate.row.has_right;
    const Attack& candidate_attack = right ? candidate.row.right : candidate.row.left;
    const double spacing = right ? profile.right_seconds : profile.chord_seconds;

    for (std::size_t index = position; index > 0;) {
        const IncrementalRow& row = rows[--index];
        const bool same_hand = right ? row.second->has_right : row.second->has_left;
        if (!same_hand) continue;
        const Attack& existing = right ? row.second->right : row.second->left;
        if (std::fabs(candidate_attack.start - existing.start) + kComparisonEpsilon < spacing) return result;
        break;
    }

    for (std::size_t index = position; index < rows.size(); ++index) {
        const IncrementalRow& row = rows[index];
        const bool same_hand = right ? row.second->has_right : row.second->has_left;
        if (!same_hand) continue;
        const Attack& existing = right ? row.second->right : row.second->left;
        if (std::fabs(candidate_attack.start - existing.start) + kComparisonEpsilon < spacing) return result;
        break;
    }

    result.action_count = current_action_count + 1u;
    result.permitted = true;
    return result;
}

bool contains_frame(const IncrementalRows& rows, const long long frame) {
    const auto found = std::lower_bound(rows.begin(), rows.end(), frame,
        [](const IncrementalRow& row, const long long value) { return row.first < value; });
    return found != rows.end() && found->first == frame;
}

std::map<long long, OutputRow> map_from_incremental_rows(const IncrementalRows& rows) {
    std::map<long long, OutputRow> result;
    for (const IncrementalRow& row : rows) result.emplace(row.first, *row.second);
    return result;
}

std::vector<Note> notes_from_rows_with_candidate(
    const IncrementalRows& rows, const IncrementalCandidate* candidate,
    const Profile& profile,
    const long long through_frame = std::numeric_limits<long long>::max()) {
    IncrementalRows materialized;
    materialized.reserve(rows.size() + (candidate != nullptr ? 1u : 0u));
    bool inserted = candidate == nullptr || candidate->frame > through_frame;
    for (const auto& entry : rows) {
        if (entry.first > through_frame) break;
        if (!inserted && candidate->frame < entry.first) {
            materialized.emplace_back(candidate->frame, &candidate->row);
            inserted = true;
        }
        materialized.push_back(entry);
    }
    if (!inserted) materialized.emplace_back(candidate->frame, &candidate->row);
    return notes_from_rows(materialized, profile);
}

std::vector<AnalysisNote> analysis_notes_from_rows_with_candidate(
    const IncrementalRows& rows, const IncrementalCandidate* candidate,
    const Profile&,
    const long long through_frame = std::numeric_limits<long long>::max()) {
    std::vector<AnalysisNote> notes;
    notes.reserve(rows.size() + (candidate != nullptr ? 1u : 0u));
    const auto append = [&](const long long, const OutputRow& row) {
        notes.push_back({row.note.beat, strain_pitch_number(row.note.pitch), row.note.chord_id,
            !row.note.pitch.empty(),
            !row.note.chord_id.empty()});
    };
    bool inserted = candidate == nullptr || candidate->frame > through_frame;
    for (const auto& entry : rows) {
        if (entry.first > through_frame) break;
        if (!inserted && candidate->frame < entry.first) {
            append(candidate->frame, candidate->row);
            inserted = true;
        }
        append(entry.first, *entry.second);
    }
    if (!inserted) append(candidate->frame, candidate->row);
    return notes;
}

double largest_gap_with_candidate(
    const IncrementalRows& rows, const IncrementalCandidate& candidate) {
    double largest_gap = 0.0;
    long long previous_frame = 0;
    bool has_previous = false;
    bool inserted = false;
    const auto consider = [&](const long long frame, double* gap, long long* previous, bool* has_value) {
        if (*has_value) *gap = std::max(*gap, static_cast<double>(frame - *previous) / 60.0);
        *previous = frame;
        *has_value = true;
    };
    for (const auto& entry : rows) {
        if (!inserted && candidate.frame < entry.first) {
            consider(candidate.frame, &largest_gap, &previous_frame, &has_previous);
            inserted = true;
        }
        consider(entry.first, &largest_gap, &previous_frame, &has_previous);
    }
    if (!inserted) consider(candidate.frame, &largest_gap, &previous_frame, &has_previous);
    return largest_gap;
}

double largest_internal_gap(const IncrementalState& state) {
    if (!std::isnan(state.largest_internal_gap)) return state.largest_internal_gap;
    if (state.deferred_candidate != nullptr) {
        state.largest_internal_gap = largest_gap_with_candidate(
            *state.rows, *state.deferred_candidate);
        return state.largest_internal_gap;
    }
    state.largest_internal_gap = 0.0;
    for (std::size_t index = 1; index < state.rows->size(); ++index) {
        state.largest_internal_gap = std::max(state.largest_internal_gap,
            static_cast<double>((*state.rows)[index].first - (*state.rows)[index - 1].first) / 60.0);
    }
    return state.largest_internal_gap;
}

bool has_only_source_rest_gaps(
    const IncrementalRows& rows,
    const std::vector<IncrementalCandidate>& candidates) {
    if (rows.empty()) return false;
    if (!candidates.empty() && rows.front().first - candidates.front().frame > 480) return false;
    for (std::size_t index = 1; index < rows.size(); ++index) {
        const IncrementalRow& previous = rows[index - 1];
        const IncrementalRow& next = rows[index];
        if (next.first - previous.first <= 480) continue;
        const bool has_bridge = std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.frame > previous.first && candidate.frame <= previous.first + 480;
        });
        if (has_bridge) return false;
    }
    return true;
}

std::size_t minimum_required_source_bridges(
    const std::map<long long, OutputRow>& rows,
    const std::vector<IncrementalCandidate>& candidates) {
    std::size_t required = 0;
    if (rows.size() < 2) return required;
    for (auto next = std::next(rows.begin()); next != rows.end(); ++next) {
        long long cursor = std::prev(next)->first;
        while (next->first - cursor > 480) {
            const long long latest_allowed = std::min(next->first - 1, cursor + 480);
            const auto upper = std::upper_bound(
                candidates.begin(), candidates.end(), latest_allowed,
                [](const long long frame, const IncrementalCandidate& candidate) {
                    return frame < candidate.frame;
                });
            if (upper == candidates.begin()) break;
            const long long bridge_frame = std::prev(upper)->frame;
            if (bridge_frame <= cursor) break;
            ++required;
            cursor = bridge_frame;
        }
    }
    return required;
}

template <typename Rows>
bool insertion_obeys_profile_spacing(
    const Rows& rows,
    const IncrementalCandidate& candidate,
    const Profile& profile) {
    const bool right = candidate.row.has_right;
    const Attack& attack = right ? candidate.row.right : candidate.row.left;
    const double seconds_floor = right ? profile.right_seconds : profile.chord_seconds;
    for (const auto& entry : rows) {
        if ((right && !entry.second.has_right) || (!right && !entry.second.has_left)) continue;
        const Attack& existing = right ? entry.second.right : entry.second.left;
        if (std::fabs(attack.start - existing.start) + kComparisonEpsilon < seconds_floor) {
            return false;
        }
    }
    return true;
}

IncrementalSelection select_incremental_rows(
    const Profile& profile,
    const double bpm,
    std::map<long long, OutputRow> baseline,
    std::vector<IncrementalCandidate> candidates,
    const std::size_t target_rows,
    const std::size_t target_minimum_rows,
    const std::size_t target_maximum_rows,
    const std::size_t preferred_baseline_actions,
    const std::size_t maximum_physical_rows) {
    IncrementalSelection result;
    const std::size_t baseline_size = baseline.size();
    const std::size_t baseline_actions = required_action_count(notes_from_rows(baseline, profile));
    const auto analyze = [&](IncrementalState* state) {
        const std::vector<AnalysisNote> notes = analysis_notes_from_rows_with_candidate(
            *state->rows, nullptr, profile);
        state->strain = calculate_joint_strain_analysis(notes, bpm, true).metrics;
        state->local_skills = calculate_local_skills_analysis(notes, bpm, true);
        if (state->route_focus < profile.route_count) {
            state->recoverable_penalty = recoverable_route_penalty(
                notes, bpm, profile.routes[state->route_focus], true);
            state->full_route_load = local_skill_violation(
                state->local_skills, profile.routes[state->route_focus]);
        }
        state->largest_internal_gap = 0.0;
        if (state->rows->size() > 1) {
            for (auto current = std::next(state->rows->begin()); current != state->rows->end(); ++current) {
                state->largest_internal_gap = std::max(state->largest_internal_gap,
                    static_cast<double>(current->first - std::prev(current)->first) / 60.0);
            }
        }
    };
    IncrementalState initial;
    auto initial_rows = std::make_shared<IncrementalRows>();
    initial_rows->reserve(baseline.size());
    for (const auto& entry : baseline) initial_rows->emplace_back(entry.first, &entry.second);
    initial.rows = std::move(initial_rows);
    initial.row_count = baseline_actions;
    initial.preferred_actions = std::min(preferred_baseline_actions, initial.rows->size());
    analyze(&initial);
    for (const auto& entry : *initial.rows) initial.salience += output_row_score(*entry.second);
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.frame != b.frame) return a.frame < b.frame;
        if (std::fabs(a.salience - b.salience) > kComparisonEpsilon) return a.salience > b.salience;
        if (a.final_right != b.final_right) return a.final_right;
        if (a.row.has_right != b.row.has_right) return a.row.has_right;
        const SourceIdentity& a_source = a.row.has_right ? a.row.right.event.source : a.row.left.event.source;
        const SourceIdentity& b_source = b.row.has_right ? b.row.right.event.source : b.row.left.event.source;
        return a_source < b_source;
    });

    constexpr std::size_t maximum_beam_width = 384;
    constexpr std::size_t states_per_route_and_row_count = 10;
    const std::size_t maximum_search_rows = std::max(baseline_actions, target_maximum_rows);
    // Bound optimization breadth, never the source timeline. Reserve one complete
    // traversal per route, then spend at most this many additional projected row
    // visits on competing states. No candidate frame is truncated at this bound.
    constexpr std::size_t maximum_projected_analysis_row_visits = 1'500'000'000u;
    std::size_t candidate_frame_count = 0;
    for (std::size_t begin = 0; begin < candidates.size();) {
        std::size_t end = begin + 1;
        while (end < candidates.size() && candidates[end].frame == candidates[begin].frame) ++end;
        ++candidate_frame_count;
        begin = end;
    }
    const std::size_t extra_states = maximum_projected_analysis_row_visits /
        std::max<std::size_t>(1, candidate_frame_count + candidates.size()) /
        std::max<std::size_t>(1, maximum_search_rows) / 2u;
    const std::size_t beam_width = std::min(maximum_beam_width, profile.route_count + extra_states);
    result.beam_width = beam_width;
    std::vector<IncrementalState> beam;
    beam.reserve(profile.route_count);
    for (std::size_t route = 0; route < profile.route_count; ++route) {
        IncrementalState focused = initial;
        focused.route_focus = route;
        focused.admissible_routes = std::uint32_t{1} << route;
        focused.recoverable_penalty = recoverable_route_penalty(
            analysis_notes_from_rows_with_candidate(*focused.rows, nullptr, profile),
            bpm, profile.routes[route], true);
        focused.full_route_load = local_skill_violation(
            focused.local_skills, profile.routes[route]);
        beam.push_back(std::move(focused));
    }
    long long first_group_frame = candidates.empty() ? 0 : candidates.front().frame;
    long long previous_group_frame = first_group_frame;
    long long total_supported_frames = 0;
    for (std::size_t begin = 0; begin < candidates.size();) {
        std::size_t end = begin + 1;
        while (end < candidates.size() && candidates[end].frame == candidates[begin].frame) ++end;
        if (begin != 0) total_supported_frames += std::min<long long>(480,
            candidates[begin].frame - previous_group_frame);
        previous_group_frame = candidates[begin].frame;
        begin = end;
    }
    long long elapsed_supported_frames = 0;
    previous_group_frame = first_group_frame;
    for (std::size_t begin = 0; begin < candidates.size();) {
        std::size_t end = begin + 1;
        while (end < candidates.size() && candidates[end].frame == candidates[begin].frame) ++end;
        if (begin != 0) elapsed_supported_frames += std::min<long long>(480,
            candidates[begin].frame - previous_group_frame);
        previous_group_frame = candidates[begin].frame;
        struct AnalysisWork {
            std::size_t skill_row_visits = 0;
            std::shared_ptr<const IncrementalRows> parent_rows;
            const IncrementalCandidate* candidate = nullptr;
            std::vector<AnalysisNote> notes;
            MidiLocalSkillMetrics local_skills;
            MidiLocalSkillMetrics finalized_prefix_skills;
            MidiLocalSkillMetrics previous_finalized_prefix_skills;
            std::size_t previous_finalized_row_count = 0;
            std::size_t finalized_row_count = 0;
            bool has_previous_finalized_prefix = false;
            bool full_analysis_unchanged = false;
        };
        struct PendingState {
            IncrementalState state;
            const IncrementalCandidate* candidate = nullptr;
            std::size_t work_index = 0;
        };
        struct WorkKey {
            const IncrementalRows* rows = nullptr;
            const IncrementalCandidate* candidate = nullptr;

            bool operator==(const WorkKey& other) const {
                return rows == other.rows && candidate == other.candidate;
            }
        };
        struct WorkKeyHash {
            std::size_t operator()(const WorkKey& value) const {
                const auto rows = reinterpret_cast<std::uintptr_t>(value.rows);
                const auto candidate = reinterpret_cast<std::uintptr_t>(value.candidate);
                return static_cast<std::size_t>(rows ^ (candidate + 0x9e3779b9u + (rows << 6) + (rows >> 2)));
            }
        };
        std::vector<PendingState> pending;
        pending.reserve(beam.size() * (end - begin + 1));
        std::vector<AnalysisWork> work;
        work.reserve(beam.size() * (end - begin + 1));
        std::unordered_map<WorkKey, std::size_t, WorkKeyHash> work_indices;
        work_indices.reserve(beam.size() * (end - begin + 1));
        const auto work_index_for = [&](const IncrementalState& state, const IncrementalCandidate* candidate,
                                        const bool full_analysis_unchanged = false) {
            const WorkKey key{state.rows.get(), candidate};
            const auto found = work_indices.find(key);
            if (found != work_indices.end()) return found->second;
            const std::size_t index = work.size();
            AnalysisWork analysis;
            analysis.parent_rows = state.rows;
            analysis.candidate = candidate;
            analysis.previous_finalized_prefix_skills = state.finalized_prefix_skills;
            analysis.previous_finalized_row_count = state.finalized_row_count;
            analysis.has_previous_finalized_prefix = state.has_finalized_prefix;
            analysis.full_analysis_unchanged = full_analysis_unchanged;
            if (full_analysis_unchanged) analysis.local_skills = state.local_skills;
            work.push_back(std::move(analysis));
            work_indices.emplace(key, index);
            return index;
        };
        std::vector<bool> spacing_eligible(end - begin, false);
        // Traversal never revisits an earlier frame. Do not let the density goal
        // discard the last opportunity to bridge an already supported source
        // interval. This is the incremental form of the final coverage check,
        // not a density minimum or an invented action.
        const auto can_skip_frame = [&](const IncrementalRows& rows) {
            const long long frame = candidates[begin].frame;
            const auto next_row = std::upper_bound(rows.begin(), rows.end(), frame,
                [](const long long time, const IncrementalRow& row) { return time < row.first; });
            long long next_frame = end < candidates.size() ? candidates[end].frame :
                std::numeric_limits<long long>::max();
            if (next_row != rows.end()) next_frame = std::min(next_frame, next_row->first);
            if (next_row == rows.begin()) return next_frame <= first_group_frame + 480;
            const long long last_frame = std::prev(next_row)->first;
            const auto first_after = std::upper_bound(candidates.begin(), candidates.end(), last_frame,
                [](const long long time, const IncrementalCandidate& candidate) { return time < candidate.frame; });
            return first_after == candidates.end() || first_after->frame > last_frame + 480 ||
                next_frame <= last_frame + 480;
        };
        for (const IncrementalState& state : beam) {
            if (can_skip_frame(*state.rows)) {
                pending.push_back(PendingState{state, nullptr, work_index_for(state, nullptr)});
            }
            if (state.rows->size() >= maximum_physical_rows ||
                contains_frame(*state.rows, candidates[begin].frame)) continue;
            for (std::size_t index = begin; index < end; ++index) {
                const InsertionSemantics insertion = analyze_incremental_insertion(
                    *state.rows, candidates[index], profile, state.row_count);
                if (!insertion.permitted) continue;
                spacing_eligible[index - begin] = true;
                IncrementalState added = state;
                const std::size_t required_additions = target_rows > baseline_actions ?
                    target_rows - baseline_actions : 0;
                const std::size_t added_row_count = insertion.action_count;
                if (added_row_count > maximum_search_rows) continue;
                const std::size_t selected_additions = added_row_count > baseline_actions ?
                    added_row_count - baseline_actions : 0;
                if (required_additions > 0) {
                    const double ideal_supported_frame = static_cast<double>(total_supported_frames) *
                        static_cast<double>(selected_additions) / static_cast<double>(required_additions);
                    added.maximum_target_timing_error = std::max(added.maximum_target_timing_error,
                        std::fabs(static_cast<double>(elapsed_supported_frames) - ideal_supported_frame) / 60.0);
                }
                added.salience += candidates[index].salience;
                added.preferred_actions += candidates[index].preferred ? 1u : 0u;
                added.row_count = added_row_count;
                const IncrementalCandidate* candidate = &candidates[index];
                const std::size_t work_index = work_index_for(
                    added, candidate, false);
                pending.push_back(PendingState{std::move(added), candidate, work_index});
            }
        }
        std::for_each(std::execution::par, work.begin(), work.end(), [&](AnalysisWork& value) {
            if (value.candidate != nullptr) {
                if (!value.full_analysis_unchanged) {
                    value.notes = analysis_notes_from_rows_with_candidate(
                        *value.parent_rows, value.candidate, profile);
                    value.local_skills = calculate_local_skills_analysis(value.notes, bpm, true);
                    value.skill_row_visits += value.notes.size();
                }
            }
            const auto finalized_end = std::upper_bound(
                value.parent_rows->begin(), value.parent_rows->end(), candidates[begin].frame,
                [](const long long frame, const IncrementalRow& row) { return frame < row.first; });
            value.finalized_row_count = static_cast<std::size_t>(
                std::distance(value.parent_rows->begin(), finalized_end)) +
                (value.candidate != nullptr ? 1u : 0u);
            if (value.candidate == nullptr && value.has_previous_finalized_prefix &&
                value.finalized_row_count == value.previous_finalized_row_count) {
                value.finalized_prefix_skills = value.previous_finalized_prefix_skills;
            } else if (value.candidate != nullptr &&
                value.finalized_row_count == value.parent_rows->size() + 1) {
                value.finalized_prefix_skills = value.local_skills;
            } else {
                const std::vector<AnalysisNote> prefix_notes = analysis_notes_from_rows_with_candidate(
                    *value.parent_rows, value.candidate, profile, candidates[begin].frame);
                value.finalized_prefix_skills = calculate_local_skills_analysis(
                    prefix_notes, bpm, true, true);
                value.skill_row_visits += prefix_notes.size();
            }
        });
        for (const auto& value : work) result.skill_row_visits += value.skill_row_visits;
        ++result.processed_frames;
        std::for_each(std::execution::par, pending.begin(), pending.end(), [&](PendingState& value) {
            const AnalysisWork& analysis = work[value.work_index];
            IncrementalState& state = value.state;
            state.deferred_candidate = value.candidate;
            state.finalized_prefix_skills = analysis.finalized_prefix_skills;
            state.finalized_row_count = analysis.finalized_row_count;
            state.has_finalized_prefix = true;
            state.admissible_routes = admissible_route_mask(state.finalized_prefix_skills, profile);
            state.finalized_route_load = local_skill_violation(
                state.finalized_prefix_skills, profile.routes[state.route_focus],
                nullptr, nullptr, nullptr, nullptr, true);
            if (value.candidate != nullptr) {
                state.largest_internal_gap = std::numeric_limits<double>::quiet_NaN();
                if (!analysis.full_analysis_unchanged) {
                    state.local_skills = analysis.local_skills;
                    state.recoverable_penalty = recoverable_route_penalty(
                        analysis.notes, bpm, profile.routes[state.route_focus], true);
                    state.full_route_load = local_skill_violation(
                        state.local_skills, profile.routes[state.route_focus]);
                }
            }
        });
        std::vector<IncrementalState> next;
        next.reserve(pending.size());
        for (PendingState& value : pending) {
            IncrementalState& state = value.state;
            if ((state.admissible_routes & (std::uint32_t{1} << state.route_focus)) == 0) {
                result.local_skill_rejections += value.candidate != nullptr ? 1u : 0u;
                continue;
            }
            state.admissible_routes = std::uint32_t{1} << state.route_focus;
            next.push_back(std::move(state));
        }
        result.spacing_rejections += static_cast<std::size_t>(std::count(
            spacing_eligible.begin(), spacing_eligible.end(), false));

        const std::size_t expected_rows = baseline_actions + static_cast<std::size_t>(std::llround(
            static_cast<double>(target_rows - std::min(target_rows, baseline_actions)) *
            static_cast<double>(elapsed_supported_frames) /
            std::max<long long>(1, total_supported_frames)));
        std::stable_sort(next.begin(), next.end(), [&](const auto& a, const auto& b) {
            const auto a_error = static_cast<std::size_t>(std::llabs(
                static_cast<long long>(a.row_count) - static_cast<long long>(expected_rows)));
            const auto b_error = static_cast<std::size_t>(std::llabs(
                static_cast<long long>(b.row_count) - static_cast<long long>(expected_rows)));
            if (a_error != b_error) return a_error < b_error;
            const bool a_feasible = a.full_route_load <=
                profile.routes[a.route_focus].evidence_margin + kComparisonEpsilon;
            const bool b_feasible = b.full_route_load <=
                profile.routes[b.route_focus].evidence_margin + kComparisonEpsilon;
            if (a_feasible != b_feasible) return a_feasible;
            // Spend feasible headroom on musical utility, not on minimizing load.
            // Still guide not-yet-feasible states toward recovery, and retain the
            // per-route feasible reservation below regardless of density error.
            if (a_feasible && std::fabs(a.salience - b.salience) > kComparisonEpsilon) {
                return a.salience > b.salience;
            }
            const double a_load = a.finalized_route_load;
            const double b_load = b.finalized_route_load;
            if (std::fabs(a_load - b_load) > kComparisonEpsilon) return a_load < b_load;
            if (std::fabs(a.recoverable_penalty - b.recoverable_penalty) > kComparisonEpsilon) {
                return a.recoverable_penalty < b.recoverable_penalty;
            }
            const double a_full_load = a.full_route_load;
            const double b_full_load = b.full_route_load;
            if (std::fabs(a_full_load - b_full_load) > kComparisonEpsilon) return a_full_load < b_full_load;
            if (std::fabs(a.maximum_target_timing_error - b.maximum_target_timing_error) > kComparisonEpsilon) {
                return a.maximum_target_timing_error < b.maximum_target_timing_error;
            }
            if (a.preferred_actions != b.preferred_actions) return a.preferred_actions > b.preferred_actions;
            if (a.rows->size() != b.rows->size()) return a.rows->size() > b.rows->size();
            const double a_largest_gap = largest_internal_gap(a);
            const double b_largest_gap = largest_internal_gap(b);
            if (std::fabs(a_largest_gap - b_largest_gap) > kComparisonEpsilon) {
                return a_largest_gap < b_largest_gap;
            }
            return a.salience > b.salience + kComparisonEpsilon;
        });
        beam.clear();
        beam.reserve(std::min(beam_width, next.size()));
        std::vector<std::array<std::size_t, 2>> route_row_counts(maximum_search_rows + 1);
        // Density-first ranking must not discard every fully feasible alternative
        // in favor of states whose recoverable/global load never recovers. Keep
        // the best ranked full-route-feasible state for each route before filling
        // the ordinary beam. It still traverses every remaining source frame.
        std::vector<bool> reserved(next.size(), false);
        for (std::size_t route = 0; route < profile.route_count; ++route) {
            for (std::size_t index = 0; index < next.size(); ++index) {
                auto& state = next[index];
                if (state.route_focus != route ||
                    state.full_route_load > profile.routes[route].evidence_margin + kComparisonEpsilon) continue;
                reserved[index] = true;
                ++route_row_counts[state.row_count][state.route_focus];
                beam.push_back(std::move(state));
                break;
            }
        }
        for (std::size_t index = 0; index < next.size() && beam.size() < beam_width; ++index) {
            if (reserved[index]) continue;
            IncrementalState& state = next[index];
            if (route_row_counts[state.row_count][state.route_focus] >=
                states_per_route_and_row_count) continue;
            ++route_row_counts[state.row_count][state.route_focus];
            beam.push_back(std::move(state));
            if (beam.size() == beam_width) break;
        }
        if (beam.empty()) {
            result.status = Status::error(StatusCode::ChartStrainLimitExceeded,
                "no target-band state satisfies a coherent local-skill route");
            return result;
        }
        std::unordered_map<WorkKey, std::shared_ptr<const IncrementalRows>, WorkKeyHash> committed_rows;
        committed_rows.reserve(beam.size());
        for (IncrementalState& state : beam) {
            if (state.deferred_candidate == nullptr) continue;
            const WorkKey key{state.rows.get(), state.deferred_candidate};
            auto found = committed_rows.find(key);
            if (found == committed_rows.end()) {
                auto selected_rows = std::make_shared<IncrementalRows>(*state.rows);
                const auto insertion = std::lower_bound(
                    selected_rows->begin(), selected_rows->end(), state.deferred_candidate->frame,
                    [](const IncrementalRow& row, const long long frame) { return row.first < frame; });
                selected_rows->emplace(
                    insertion, state.deferred_candidate->frame, &state.deferred_candidate->row);
                found = committed_rows.emplace(key, std::move(selected_rows)).first;
            }
            state.rows = found->second;
            state.deferred_candidate = nullptr;
        }
        begin = end;
    }

    const IncrementalState* best = nullptr;
    for (const IncrementalState& state : beam) {
        if (state.row_count == 0 || state.rows->size() > maximum_physical_rows ||
            best_route_violation(state.local_skills, profile) > 1.0 + kComparisonEpsilon ||
            !has_only_source_rest_gaps(*state.rows, candidates)) continue;
        const auto state_error = std::llabs(
            static_cast<long long>(state.row_count) - static_cast<long long>(target_rows));
        const auto best_error = best ? std::llabs(
            static_cast<long long>(best->row_count) - static_cast<long long>(target_rows)) : 0;
        const double state_load = best_route_violation(state.local_skills, profile);
        const double best_load = best ? best_route_violation(best->local_skills, profile) : 0.0;
        const auto better = [&] {
            if (!best) return true;
            if (state_error != best_error) return state_error < best_error;
            if (std::fabs(state.salience - best->salience) > kComparisonEpsilon) {
                return state.salience > best->salience;
            }
            if (std::fabs(state_load - best_load) > kComparisonEpsilon) return state_load < best_load;
            if (std::fabs(state.maximum_target_timing_error - best->maximum_target_timing_error) > kComparisonEpsilon) {
                return state.maximum_target_timing_error < best->maximum_target_timing_error;
            }
            if (state.preferred_actions != best->preferred_actions) return state.preferred_actions > best->preferred_actions;
            if (state.rows->size() != best->rows->size()) return state.rows->size() > best->rows->size();
            return largest_internal_gap(state) + kComparisonEpsilon < largest_internal_gap(*best);
        };
        if (better()) {
            best = &state;
        }
    }
    if (!best) {
        const IncrementalState* witness = nullptr;
        for (const IncrementalState& state : beam) {
            const auto distance = [&](const IncrementalState& value) {
            const std::size_t row_distance = value.row_count < target_minimum_rows ?
                target_minimum_rows - value.row_count : value.row_count > target_maximum_rows ?
                value.row_count - target_maximum_rows : 0;
                return std::tuple<std::size_t, double, double, double>{
                    row_distance,
                    best_route_violation(value.local_skills, profile),
                    largest_internal_gap(value), -value.salience};
            };
            if (!witness || distance(state) < distance(*witness)) witness = &state;
        }
        if (!witness) witness = &beam.front();
        result.rows = map_from_incremental_rows(*witness->rows);
        const std::vector<AnalysisNote> witness_notes = analysis_notes_from_rows_with_candidate(
            *witness->rows, nullptr, profile);
        result.strain = calculate_joint_strain_analysis(witness_notes, bpm, true).metrics;
        result.local_skills = calculate_local_skills_analysis(witness_notes, bpm);
        annotate_best_route(&result.local_skills, profile);
        const std::string interval = result.local_skills.dominant_skill_is_global ? "global" :
            std::to_string(result.local_skills.hardest_window_begin_seconds) + "-" +
                std::to_string(result.local_skills.hardest_window_end_seconds);
        result.status = Status::error(StatusCode::ChartStrainLimitExceeded,
            "near-feasible witness: rows=" + std::to_string(witness->rows->size()) +
            " preferred=" + std::to_string(witness->preferred_actions) +
            " preferred_rows=" + std::to_string(target_minimum_rows) +
            ".." + std::to_string(target_maximum_rows) +
            " coverage=" + (has_only_source_rest_gaps(*witness->rows, candidates) ? "complete" : "missing_source_bridge") +
            " route=" + result.local_skills.satisfied_route_name +
            " ratio=" + std::to_string(result.local_skills.satisfied_route_ratio) +
            " margin=" + std::to_string(result.local_skills.satisfied_route_margin) +
            " dominant_skill=" + std::to_string(result.local_skills.dominant_skill) +
            " interval=" + interval);
        return result;
    }
    result.rows = map_from_incremental_rows(*best->rows);
    result.strain = calculate_joint_strain_analysis(
        analysis_notes_from_rows_with_candidate(*best->rows, nullptr, profile), bpm, true).metrics;
    result.local_skills = best->local_skills;
    annotate_best_route(&result.local_skills, profile);
    result.retained_actions = best->preferred_actions;
    const std::size_t additions = result.rows.size() - baseline_size;
    result.rejected_candidates = candidates.size() > additions ? candidates.size() - additions : 0;
    result.target_exclusions = candidates.size() > additions + result.local_skill_rejections ?
        candidates.size() - additions - result.local_skill_rejections : 0;
    return result;
}

} // namespace

std::string infer_native_chord_from_fresh_midi_pitches(const std::vector<int>& midi_pitches) {
    std::set<int> fresh_pitch_classes;
    for (const int pitch : midi_pitches) {
        if (pitch >= 0 && pitch <= 127) fresh_pitch_classes.insert(pitch % 12);
    }
    return infer_native_chord_match(
        fresh_pitch_classes, false, selected_native_asset_capabilities()).id;
}

std::array<int, 2> vanilla_mode_change_counts_for_route(const std::string_view route_name) {
    if (route_name == "Journey") return {5, 10};
    if (route_name == "Tifa") return {14, 24};
    if (route_name == "Barret") return {12, 20};
    if (route_name == "Synco") return {10, 20};
    if (route_name == "Difficult") return {11, 22};
    if (route_name == "Aerith") return {10, 20};
    if (route_name == "Fighters") return {15, 30};
    if (route_name == "OneWingedAngel") return {8, 24};
    return {0, 0};
}

MidiJointStrainMetrics analyze_midi_joint_strain(const std::vector<Note>& notes, const double bpm) {
    return calculate_joint_strain(notes, bpm).metrics;
}

MidiDifficultyEnvelope midi_difficulty_envelope(const int difficulty) {
    if (difficulty < kLowestMidiDifficulty || difficulty > kHighestMidiDifficulty) return {};
    const Profile profile = profile_for_difficulty(difficulty);
    return {profile.maximum_joint_strain_p95, profile.maximum_joint_strain_peak};
}

MidiDifficultySpec midi_difficulty_spec(const int difficulty) {
    if (difficulty < kLowestMidiDifficulty || difficulty > kHighestMidiDifficulty) return {};
    const Profile profile = profile_for_difficulty(difficulty);
    return {profile.target_actions_per_minute, profile.target_tolerance, profile.route_count};
}

MidiLocalSkillMetrics analyze_midi_local_skills(const std::vector<Note>& notes, const double bpm) {
    return calculate_local_skills(notes, bpm);
}

MidiRouteValidation validate_midi_difficulty_route(
    const std::vector<Note>& notes, const double bpm, const int difficulty) {
    MidiRouteValidation validation;
    if (difficulty < kLowestMidiDifficulty || difficulty > kHighestMidiDifficulty || bpm <= 0.0) {
        return validation;
    }
    const Profile profile = profile_for_difficulty(difficulty);
    validation.metrics = calculate_local_skills(notes, bpm);
    annotate_best_route(&validation.metrics, profile);
    validation.route_index = validation.metrics.satisfied_route;
    validation.ratio = validation.metrics.satisfied_route_ratio;
    validation.margin = validation.metrics.satisfied_route_margin;
    validation.feasible = validation.route_index >= 0 &&
        validation.ratio <= validation.margin + kComparisonEpsilon;
    return validation;
}

std::size_t minimum_midi_profile_growth(const std::size_t previous_actions) {
    const auto relative = static_cast<std::size_t>(std::ceil(
        static_cast<double>(previous_actions) * kMinimumRelativeProfileGrowth));
    return std::max(kMinimumAbsoluteProfileGrowth, relative);
}

bool has_meaningful_midi_profile_growth(
    const std::size_t previous_actions,
    const std::size_t next_actions) {
    return next_actions >= previous_actions + minimum_midi_profile_growth(previous_actions);
}

std::size_t maximum_midi_visible_profile_actions(const std::size_t previous_actions) {
    return static_cast<std::size_t>(std::floor(
        static_cast<double>(previous_actions) * (1.0 + kMaximumRelativeVisibleProfileGrowth) +
        kComparisonEpsilon));
}

MidiChartCompilationResult compile_normalized_midi_chart(
    const MidiChartCompilationRequest& request) {
    const NormalizedMidiSource& normalized_source = request.source;
    const WavAudio& audio = request.audio;
    const SongConfig& config = request.config;
    const std::vector<Note>* preferred_baseline = request.preferred_baseline;
    // Preference and growth describe inputs, never the automatic sound rows.
    std::vector<Note> preferred_roots;
    if (preferred_baseline) {
        std::uint8_t previous_group = 0;
        for (const Note& note : *preferred_baseline) {
            const bool follower = note.group_index != 0 && note.group_index == previous_group;
            previous_group = note.group_index;
            if (!follower) preferred_roots.push_back(note);
        }
        preferred_baseline = &preferred_roots;
    }
    const std::size_t maximum_visible_rows = request.maximum_visible_rows;
    MidiChartCompilationResult result;
    if (!config.chord_voicings.empty()) {
        result.status = Status::error(StatusCode::InvalidChart,
            "chord_voicings cannot revoice automatic MIDI inference; export resolved-song.json then author the chart");
        return result;
    }
    std::vector<Note>* out_notes = &result.notes;
    MidiChartStats* out_stats = &result.stats;
    const int ticks_per_quarter = normalized_source.ticks_per_quarter;
    const double source_bpm = normalized_source.source_bpm;
    std::vector<TempoChange> tempos = normalized_source.tempos;
    std::vector<MeterChange> meters = normalized_source.meters;
    std::vector<MidiNoteEvent> source = normalized_source.notes;
    const std::vector<MidiAccidentalOrientationChange> accidental_orientation =
        build_midi_accidental_orientation_timeline(normalized_source.key_signatures);
    const double chart_bpm = config.bpm_provided ? config.bpm : source_bpm;
    std::size_t exact_groups = 0;
    std::size_t humanized_events = 0;
    const std::vector<OnsetCluster> clusters = build_onset_clusters(
        source, ticks_per_quarter, tempos, meters, &exact_groups, &humanized_events);
    MidiMelodyTrackingResult melody = track_midi_melody_voice(clusters);
    std::vector<Attack> voice = std::move(melody.attacks);
    const std::size_t stream_changes = melody.stream_changes;
    // Keep the established humanized context for voice tracking, then restore
    // every selected source event to its authoritative timing before filtering,
    // prominence measurement, and per-profile reduction.
    for (Attack& attack : voice) {
        attack.start = attack.event.start;
        attack.beat = attack.event.beat;
    }
    const std::vector<Attack> alignment_attacks = build_alignment_attacks(clusters);
    const MidiAudioAlignmentResult estimated_alignment = config.midi_audio_alignment_provided ?
        MidiAudioAlignmentResult{config.midi_audio_alignment_seconds, 1.0} :
        estimate_midi_audio_alignment(audio, alignment_attacks);
    const double audio_alignment_seconds = config.midi_audio_alignment_provided ?
        config.midi_audio_alignment_seconds : estimated_alignment.seconds;
    const double audio_offset_seconds = config.midi_audio_offset_provided ?
        config.midi_audio_offset_seconds : audio_alignment_seconds + kAudioPlaybackDelaySeconds;
    const auto native_frame = [audio_offset_seconds](const double seconds) {
        return static_cast<long long>(std::llround((seconds + audio_offset_seconds) * 60.0));
    };
    const long long minimum_frame = static_cast<long long>(std::ceil(
        config.midi_minimum_lead_in_seconds * 60.0 - kComparisonEpsilon));
    const double audio_duration_seconds = audio.source_duration_seconds();
    const bool has_known_audio_duration = audio.sample_rate != 0 && audio.source_frame_count != 0;
    const long long maximum_audio_frame = has_known_audio_duration ?
        static_cast<long long>(std::floor(audio_duration_seconds * 60.0 + kComparisonEpsilon)) :
        std::numeric_limits<long long>::max();

    std::set<SourceIdentity> melody_sources;
    std::vector<Attack> primary;
    for (const Attack& attack : voice) {
        melody_sources.insert(attack.event.source);
        if (attack.melody_evidence >= kPrimaryVoiceEvidence) primary.push_back(attack);
    }
    const auto timing_rejection = [&](const Attack& attack) {
        const double source_seconds = attack.start + audio_offset_seconds;
        const long long frame = native_frame(attack.start);
        if (frame < minimum_frame) return 1;
        if (has_known_audio_duration &&
            (source_seconds > audio_duration_seconds + kComparisonEpsilon || frame > maximum_audio_frame)) {
            return 2;
        }
        return 0;
    };
    Profile profile = profile_for_difficulty(config.difficulty);
    const std::map<SourceIdentity, bool> alternate_monotone_plan = plan_alternate_monotones(voice);
    const ChartRowPolicySnapshot physical_policy = chart_row_policy_snapshot();
    // Normalization excludes unsupported attacks for every row policy. Their
    // presence does not invalidate the remaining source-backed candidates.
    std::vector<Attack> fallback = build_fallback_candidates(
        clusters, voice, melody_sources);
    std::vector<Attack> chords = build_chord_candidates(
        clusters, melody_sources, accidental_orientation, request.native_assets);
    std::set<SourceIdentity> lead_in_rejection_sources;
    std::set<SourceIdentity> audio_duration_rejection_sources;
    const auto count_timing_rejections = [&](const std::vector<Attack>& attacks) {
        for (const Attack& attack : attacks) {
            const int rejection = timing_rejection(attack);
            if (rejection == 1) lead_in_rejection_sources.insert(attack.event.source);
            if (rejection == 2) audio_duration_rejection_sources.insert(attack.event.source);
        }
    };
    count_timing_rejections(voice);
    count_timing_rejections(fallback);
    count_timing_rejections(chords);
    const auto filter_timing_domain = [&](std::vector<Attack>* attacks) {
        attacks->erase(std::remove_if(attacks->begin(), attacks->end(), [&](const Attack& attack) {
            return timing_rejection(attack) != 0;
        }), attacks->end());
    };
    filter_timing_domain(&primary);
    filter_timing_domain(&fallback);
    filter_timing_domain(&chords);
    std::vector<Attack> eligible_voice = voice;
    filter_timing_domain(&eligible_voice);
    const std::size_t lead_in_rejections = lead_in_rejection_sources.size();
    const std::size_t audio_duration_rejections = audio_duration_rejection_sources.size();
    if (out_stats) {
        out_stats->lead_in_rejections = lead_in_rejections;
        out_stats->audio_duration_rejections = audio_duration_rejections;
    }
    measure_shared_audio_prominence(audio, {&primary, &fallback, &chords}, audio_alignment_seconds);

    std::vector<Attack> right_candidates = primary;
    right_candidates.insert(right_candidates.end(), fallback.begin(), fallback.end());
    std::sort(right_candidates.begin(), right_candidates.end(), attack_less);
    right_candidates.erase(std::unique(right_candidates.begin(), right_candidates.end(),
        [](const Attack& a, const Attack& b) { return a.event.source == b.event.source; }),
        right_candidates.end());
    const std::size_t unfiltered_right_count = right_candidates.size();
    right_candidates.erase(std::remove_if(right_candidates.begin(), right_candidates.end(),
        [&](const Attack& attack) {
            return attack_evidence(attack) + kComparisonEpsilon < profile.evidence;
        }), right_candidates.end());
    if (right_candidates.empty() && !eligible_voice.empty()) {
        right_candidates.push_back(*std::max_element(eligible_voice.begin(), eligible_voice.end(),
            [](const Attack& a, const Attack& b) { return attack_better(b, a); }));
    }
    const Attack* final_voice = eligible_voice.empty() ? nullptr : &*std::max_element(
        eligible_voice.begin(), eligible_voice.end(),
        [](const Attack& a, const Attack& b) { return attack_less(a, b); });
    if (final_voice && std::none_of(right_candidates.begin(), right_candidates.end(),
            [&](const Attack& attack) { return attack.event.source == final_voice->event.source; })) {
        right_candidates.push_back(*final_voice);
    }
    std::sort(right_candidates.begin(), right_candidates.end(), attack_less);
    annotate_rhythmic_quality(&right_candidates, profile.right_seconds, profile.right_beats);
    annotate_rhythmic_quality(&chords, profile.chord_seconds, profile.chord_beats);
    double source_duration = 0.0;
    for (const MidiNoteEvent& note : source) source_duration = std::max(source_duration, note.end);
    SelectionStats selection_stats;
    selection_stats.evidence_rejections = unfiltered_right_count - right_candidates.size() +
        static_cast<std::size_t>(final_voice &&
            attack_evidence(*final_voice) + kComparisonEpsilon < profile.evidence);

    std::map<long long, std::vector<OutputAction>> desired_actions;
    SourceIdentity final_right_source;
    const bool has_final_right = final_voice != nullptr;
    if (final_voice) final_right_source = final_voice->event.source;
    for (const Attack& attack : right_candidates) {
        const long long time = native_frame(attack.start);
        if (time < minimum_frame || time > maximum_audio_frame) continue;
        desired_actions[time].push_back({time, true,
            has_final_right && attack.event.source == final_right_source, attack});
    }
    for (const Attack& attack : chords) {
        const long long time = native_frame(attack.start);
        if (time < minimum_frame || time > maximum_audio_frame) continue;
        desired_actions[time].push_back({time, false, false, attack});
    }
    if (desired_actions.empty()) {
        result.status = Status::error(StatusCode::InvalidChart,
            "MIDI generation produced no chart rows inside the lead-in/audio timing domain");
        return result;
    }
    std::size_t selected_actions = 0;
    for (const auto& entry : desired_actions) selected_actions += entry.second.size();

    const auto action_better = [](const OutputAction& a, const OutputAction& b) {
        if (a.final_right != b.final_right) return a.final_right;
        const double a_score = attack_score(a.attack);
        const double b_score = attack_score(b.attack);
        if (std::fabs(a_score - b_score) > kComparisonEpsilon) return a_score > b_score;
        if (a.right != b.right) return a.right;
        return a.attack.event.source < b.attack.event.source;
    };
    const auto make_row = [chart_bpm, &alternate_monotone_plan, &accidental_orientation,
                              ticks_per_quarter](
                              const long long frame, const OutputAction& action) {
        OutputRow row;
        row.note.beat = (static_cast<double>(frame) / 60.0) * chart_bpm / 60.0;
        row.note.duration_beats = 0.25;
        if (action.right) {
            row.has_right = true;
            row.right = action.attack;
            row.note.pitch = generated_pitch_name(action.attack.event.source.pitch,
                midi_accidental_orientation_at_tick(
                    accidental_orientation, action.attack.event.source.tick));
            const auto planned = alternate_monotone_plan.find(action.attack.event.source);
            const bool flat_spelling = row.note.pitch.size() == 3u && row.note.pitch[1] == 'b';
            row.note.alternate_monotone = !flat_spelling &&
                planned != alternate_monotone_plan.end() && planned->second;
            row.note.monotone_note_value = exact_midi_note_value(
                action.attack.event.source, ticks_per_quarter);
        } else {
            row.has_left = true;
            row.left = action.attack;
            row.note.chord_id = action.attack.chord_id;
            row.note.ignore_sound_pitches = action.attack.ignore_sound_pitches;
            row.note.source_chord_pitches = action.attack.source_chord_pitches;
            row.note.chord_note_value = exact_midi_chord_note_value(
                action.attack.chord_sources, ticks_per_quarter);
        }
        return row;
    };

    std::map<long long, OutputRow> baseline_rows;
    std::map<long long, Note> preferred_rows;
    const auto matches_note = [&](const OutputAction& action, const Note& note) {
        return action.right ?
            note.chord_id.empty() && note.pitch == generated_pitch_name(
                action.attack.event.source.pitch, midi_accidental_orientation_at_tick(
                    accidental_orientation, action.attack.event.source.tick)) &&
                note.monotone_note_value == exact_midi_note_value(
                    action.attack.event.source, ticks_per_quarter) :
            note.pitch.empty() && note.chord_id == action.attack.chord_id &&
                note.chord_note_value == exact_midi_chord_note_value(
                    action.attack.chord_sources, ticks_per_quarter);
    };
    if (preferred_baseline) {
        for (const Note& note : *preferred_baseline) {
            const long long frame = static_cast<long long>(std::llround(
                note.beat * 60.0 / chart_bpm * 60.0));
            const auto actions = desired_actions.find(frame);
            if (frame < minimum_frame || frame > maximum_audio_frame || actions == desired_actions.end()) {
                result.status = Status::error(StatusCode::InvalidChart,
                    "preferred lower-profile action has no source candidate at its native frame");
                return result;
            }
            const auto match = std::find_if(actions->second.begin(), actions->second.end(),
                [&](const OutputAction& action) { return matches_note(action, note); });
            if (match == actions->second.end()) {
                result.status = Status::error(StatusCode::InvalidChart,
                    "preferred lower-profile action lost its exact source witness");
                return result;
            }
            if (!preferred_rows.emplace(frame, note).second) {
                result.status = Status::error(StatusCode::InvalidChart,
                    "preferred lower profile contains multiple actions on one native frame");
                return result;
            }
        }
    }
    if (final_voice) {
        const long long final_frame = native_frame(final_voice->start);
        const auto actions = desired_actions.find(final_frame);
        const auto final_action = actions == desired_actions.end() ? std::vector<OutputAction>::const_iterator{} :
            std::find_if(actions->second.cbegin(), actions->second.cend(), [&](const OutputAction& action) {
                return action.right && action.attack.event.source == final_right_source;
            });
        if (actions == desired_actions.end() || final_action == actions->second.cend()) {
            result.status = Status::error(StatusCode::InvalidChart, "final melody onset lost its source candidate");
            return result;
        }
        const auto existing = baseline_rows.find(final_frame);
        if (existing == baseline_rows.end()) {
            OutputRow row = make_row(final_frame, *final_action);
            row.protected_action = true;
            baseline_rows.emplace(final_frame, std::move(row));
        }
    }

    std::vector<IncrementalCandidate> incremental_candidates;
    std::size_t right_collisions = 0;
    std::size_t left_collisions = 0;
    std::size_t cross_hand_conflicts = 0;
    for (auto& [frame, actions] : desired_actions) {
        const std::size_t right_count = static_cast<std::size_t>(std::count_if(
            actions.begin(), actions.end(), [](const OutputAction& action) { return action.right; }));
        const std::size_t left_count = actions.size() - right_count;
        if (right_count > 1) right_collisions += right_count - 1;
        if (left_count > 1) left_collisions += left_count - 1;
        if (right_count > 0 && left_count > 0) ++cross_hand_conflicts;
        std::sort(actions.begin(), actions.end(), action_better);
        if (baseline_rows.count(frame) != 0) continue;
        for (const OutputAction& action : actions) {
            IncrementalCandidate candidate;
            candidate.frame = frame;
            candidate.row = make_row(frame, action);
            candidate.salience = attack_score(action.attack);
            candidate.final_right = action.final_right;
            const auto preferred = preferred_rows.find(frame);
            candidate.preferred = preferred != preferred_rows.end() && matches_note(action, preferred->second);
            incremental_candidates.push_back(std::move(candidate));
        }
    }

    const std::size_t scheduled_conflicts = 0;
    std::size_t dropped_conflicts = 0;
    for (const auto& entry : desired_actions) {
        if (entry.second.size() > 1) dropped_conflicts += entry.second.size() - 1;
    }
    const long long first_frame = desired_actions.begin()->first;
    long long supported_frames = 0;
    long long previous_supported_frame = first_frame;
    for (const auto& entry : desired_actions) {
        if (entry.first == first_frame) continue;
        supported_frames += std::min<long long>(480, entry.first - previous_supported_frame);
        previous_supported_frame = entry.first;
    }
    const double active_seconds = std::max(0.0, static_cast<double>(supported_frames) / 60.0);
    std::map<long long, OutputRow> complete_candidate_rows;
    for (const auto& [frame, actions] : desired_actions) {
        if (!actions.empty()) complete_candidate_rows.emplace(frame, make_row(frame, actions.front()));
    }
    const std::size_t available_actions = required_action_count(notes_from_rows(complete_candidate_rows, profile));
    std::size_t target_rows = std::min(available_actions, std::max<std::size_t>(1,
        static_cast<std::size_t>(std::llround(active_seconds * profile.target_actions_per_minute / 60.0)) + 1));
    std::size_t target_minimum_rows = std::min(available_actions, std::max<std::size_t>(1,
        static_cast<std::size_t>(std::floor(target_rows * (1.0 - profile.target_tolerance)))));
    std::size_t target_maximum_rows = std::min(available_actions, std::max(target_minimum_rows,
        static_cast<std::size_t>(std::ceil(target_rows * (1.0 + profile.target_tolerance)))));
    const std::size_t chart_row_limit = physical_policy.playable_extended_available
        ? std::min(kMaximumExtendedChartRows, kMaximumNativeChartEvents)
        : effective_chart_row_limit();
    // Density and adjacent growth shape the optimization goal, not feasibility.
    // Keep a coherent preferred band even when native capacity is the tighter goal.
    if (maximum_visible_rows != 0) {
        target_rows = std::min(target_rows, maximum_visible_rows);
    }
    target_rows = std::min(target_rows, chart_row_limit);
    target_minimum_rows = std::min(target_minimum_rows, target_rows);
    target_maximum_rows = std::min(target_maximum_rows, chart_row_limit);
    IncrementalSelection selection = select_incremental_rows(profile, chart_bpm, baseline_rows,
        incremental_candidates, target_rows, target_minimum_rows, target_maximum_rows,
        preferred_baseline ? preferred_baseline->size() : 0, chart_row_limit);
    if (out_stats) {
        out_stats->selector_beam_width = selection.beam_width;
        out_stats->selector_processed_frames = selection.processed_frames;
        out_stats->selector_skill_row_visits = selection.skill_row_visits;
    }
    if (!selection.status.ok()) {
        *out_notes = notes_from_rows(selection.rows, profile);
        if (out_stats) {
            out_stats->desired_rows = selection.rows.size();
            out_stats->right_events = static_cast<std::size_t>(std::count_if(
                selection.rows.begin(), selection.rows.end(), [](const auto& entry) {
                    return entry.second.has_right;
                }));
            out_stats->left_events = selection.rows.size() - out_stats->right_events;
            out_stats->selected_actions = required_action_count(notes_from_rows(selection.rows, profile));
            out_stats->row_limit_exceeded =
                selection.status.code == StatusCode::ChartRowLimitExceeded;
            out_stats->joint_strain_p95 = selection.strain.p95;
            out_stats->joint_strain_peak = selection.strain.peak;
            out_stats->local_skills = selection.local_skills;
            out_stats->candidate_actions = incremental_candidates.size();
            out_stats->candidate_frames = desired_actions.size();
            out_stats->target_exclusions = selection.target_exclusions;
            out_stats->local_skill_rejections = selection.local_skill_rejections;
            out_stats->source_bpm = chart_bpm;
            out_stats->protected_baseline_actions = preferred_baseline ? preferred_baseline->size() : 0;
            out_stats->target_rows = target_rows;
            out_stats->target_minimum_rows = target_minimum_rows;
            out_stats->target_maximum_rows = target_maximum_rows;
        }
        result.status = selection.status;
        return result;
    }
    std::map<long long, OutputRow> rows = std::move(selection.rows);
    const MidiJointStrainMetrics joint_strain = selection.strain;
    const MidiRouteValidation route_validation = validate_midi_difficulty_route(
        notes_from_rows(rows, profile), chart_bpm, config.difficulty);
    MidiLocalSkillMetrics local_skills = route_validation.metrics;
    if (!route_validation.feasible) {
        *out_notes = notes_from_rows(rows, profile);
        if (out_stats) {
            out_stats->desired_rows = rows.size();
            out_stats->selected_actions = required_action_count(notes_from_rows(rows, profile));
            out_stats->local_skills = local_skills;
            out_stats->target_rows = target_rows;
            out_stats->target_minimum_rows = target_minimum_rows;
            out_stats->target_maximum_rows = target_maximum_rows;
        }
        result.status = Status::error(StatusCode::ChartStrainLimitExceeded,
            "shared full-route validator rejected reconstructed chart");
        return result;
    }
    selection_stats.cooldown_rejections = static_cast<std::size_t>(std::count_if(
        incremental_candidates.begin(), incremental_candidates.end(), [&](const auto& candidate) {
            return rows.count(candidate.frame) == 0 &&
                !insertion_obeys_profile_spacing(rows, candidate, profile);
        }));
    out_notes->clear();
    out_notes->reserve(rows.size());
    // Exact tick/track/channel is the authority, not the humanized onset cluster
    // or rounded native frame. Index once and retain stable pitch/source order.
    using OnsetStream = std::tuple<int, int, int>;
    std::map<OnsetStream, std::vector<const MidiNoteEvent*>> simultaneous;
    for (const auto& event : source)
        simultaneous[{event.source.tick, event.source.track, event.source.channel}].push_back(&event);
    for (auto& [key, events] : simultaneous) {
        std::sort(events.begin(), events.end(), [](const auto* a, const auto* b) {
            if (a->source.pitch != b->source.pitch) return a->source.pitch < b->source.pitch;
            return a->source < b->source;
        });
    }
    // Inferred LH cluster timing can precede its constituent ticks. Exclude only
    // actual unmuted native sounds backed by that selected accompaniment onset.
    std::map<int, std::set<int>> selected_left_sounds;
    for (const auto& [frame, row] : rows) {
        if (!row.has_left) continue;
        const auto* chord = find_verified_native_chord(row.note.chord_id, request.native_assets);
        if (!chord) continue;
        for (const auto& constituent : row.left.chord_sources) {
            for (std::size_t i = 0; i < chord->sound_count; ++i) {
                const std::string sound(chord->sound_names[i]);
                if (std::find(row.note.ignore_sound_pitches.begin(), row.note.ignore_sound_pitches.end(), sound)
                    == row.note.ignore_sound_pitches.end())
                    selected_left_sounds[constituent.tick].insert(strain_pitch_number(sound));
            }
        }
    }
    std::uint8_t next_group = 1;
    for (const auto& [frame, row] : rows) {
        const std::size_t root_index = out_notes->size();
        out_notes->push_back(row.note);
        if (!row.has_right) continue;
        const auto& root = row.right.event.source;
        std::set<int> emitted{root.pitch};
        const auto covered = selected_left_sounds.find(root.tick);
        if (covered != selected_left_sounds.end()) emitted.insert(covered->second.begin(), covered->second.end());
        for (const auto* event : simultaneous.at({root.tick, root.track, root.channel})) {
            if (!emitted.insert(event->source.pitch).second) continue;
            OutputAction follower;
            follower.right = true;
            follower.attack.event = *event;
            Note note = make_row(frame, follower).note;
            note.group_index = next_group;
            out_notes->push_back(std::move(note));
        }
        if (out_notes->size() != root_index + 1) {
            (*out_notes)[root_index].group_index = next_group;
            next_group = next_group == 1 ? 2 : 1;
        }
    }
    if (out_stats) {
        std::set<int> source_tracks;
        std::set<SourceIdentity> source_identities;
        std::set<std::pair<int, int>> melody_streams;
        std::set<std::pair<int, int>> harmony_streams;
        for (const MidiNoteEvent& note : source) {
            source_tracks.insert(note.source.track);
            source_identities.insert(note.source);
            const std::pair<int, int> stream{note.source.track, note.source.channel};
            if (melody_sources.find(note.source) != melody_sources.end()) melody_streams.insert(stream);
            else harmony_streams.insert(stream);
        }
        std::size_t emitted_right = 0;
        std::size_t emitted_left = 0;
        std::size_t emitted_fallback = 0;
        std::size_t merged = 0;
        std::size_t selected_downbeats = 0;
        std::size_t source_pitch_witness_failures = 0;
        std::vector<Attack> emitted_right_attacks;
        for (const auto& entry : rows) {
            if (entry.second.has_right) {
                ++emitted_right;
                emitted_right_attacks.push_back(entry.second.right);
                if (entry.second.right.fallback) ++emitted_fallback;
                if (entry.second.right.metric_accent >= 1.0 - kComparisonEpsilon) ++selected_downbeats;
                if (source_identities.find(entry.second.right.event.source) == source_identities.end()) {
                    ++source_pitch_witness_failures;
                }
            }
            if (entry.second.has_left) ++emitted_left;
            if (entry.second.has_right && entry.second.has_left) ++merged;
        }
        std::sort(emitted_right_attacks.begin(), emitted_right_attacks.end(), attack_less);
        double minimum_seconds = 0.0;
        double minimum_beats = 0.0;
        if (emitted_right_attacks.size() > 1) {
            minimum_seconds = std::numeric_limits<double>::infinity();
            minimum_beats = std::numeric_limits<double>::infinity();
            for (std::size_t i = 1; i < emitted_right_attacks.size(); ++i) {
                minimum_seconds = std::min(minimum_seconds,
                    emitted_right_attacks[i].start - emitted_right_attacks[i - 1].start);
                minimum_beats = std::min(minimum_beats,
                    emitted_right_attacks[i].beat - emitted_right_attacks[i - 1].beat);
            }
        }
        std::size_t explicit_meters = 0;
        for (const MeterChange& meter : meters) if (meter.explicit_event) ++explicit_meters;
        std::size_t downbeat_candidates = 0;
        for (const OnsetCluster& cluster : clusters) {
            if (cluster.metric_accent >= 1.0 - kComparisonEpsilon) ++downbeat_candidates;
        }
        out_stats->source_tracks = source_tracks.size();
        out_stats->source_events = source.size();
        out_stats->melody_tracks = melody_streams.size();
        out_stats->harmony_tracks = harmony_streams.size();
        out_stats->melody_onsets = voice.size();
        out_stats->exact_tick_groups = exact_groups;
        out_stats->humanized_clusters = clusters.size();
        out_stats->humanized_events = humanized_events;
        out_stats->melody_candidates = primary.size();
        out_stats->fallback_candidates = fallback.size();
        out_stats->chord_candidates = chords.size();
        out_stats->right_events = emitted_right + out_notes->size() - rows.size();
        out_stats->left_events = emitted_left;
        out_stats->fallback_events = emitted_fallback;
        out_stats->merged_events = merged;
        out_stats->evidence_rejections = selection_stats.evidence_rejections;
        out_stats->cooldown_rejections = selection_stats.cooldown_rejections;
        out_stats->burst_rejections = selection_stats.burst_rejections;
        out_stats->strain_rejections = 0;
        out_stats->retention_rejections = selection_stats.retention_rejections;
        out_stats->action_rate_rejections = selection_stats.action_rate_rejections;
        out_stats->right_collisions = right_collisions;
        out_stats->left_collisions = left_collisions;
        out_stats->cross_hand_conflicts = cross_hand_conflicts;
        out_stats->scheduled_conflicts = scheduled_conflicts;
        out_stats->dropped_conflicts = dropped_conflicts;
        out_stats->selected_actions = required_action_count(*out_notes);
        out_stats->candidate_actions = selected_actions;
        out_stats->candidate_frames = desired_actions.size();
        out_stats->protected_baseline_actions = preferred_baseline ? preferred_baseline->size() : 0;
        out_stats->target_rows = target_rows;
        out_stats->target_minimum_rows = target_minimum_rows;
        out_stats->target_maximum_rows = target_maximum_rows;
        out_stats->target_exclusions = selection.target_exclusions;
        out_stats->local_skill_rejections = selection.local_skill_rejections;
        out_stats->retained_actions = selection.retained_actions;
        out_stats->removed_actions = preferred_baseline && preferred_baseline->size() > selection.retained_actions ?
            preferred_baseline->size() - selection.retained_actions : 0;
        out_stats->added_actions = rows.size() > selection.retained_actions ?
            rows.size() - selection.retained_actions : 0;
        out_stats->local_skills = local_skills;
        out_stats->desired_rows = out_notes->size();
        out_stats->lead_in_rejections = lead_in_rejections;
        out_stats->audio_duration_rejections = audio_duration_rejections;
        out_stats->row_limit_exceeded = out_notes->size() > chart_row_limit;
        out_stats->voice_stream_changes = stream_changes;
        out_stats->time_signature_changes = explicit_meters;
        out_stats->metric_downbeat_candidates = downbeat_candidates;
        out_stats->metric_downbeat_right_events = selected_downbeats;
        out_stats->source_pitch_witness_failures = source_pitch_witness_failures;
        out_stats->octave_fixes = 0;
        out_stats->source_bpm = source_bpm;
        out_stats->selected_retention = right_candidates.empty() ? 0.0 :
            static_cast<double>(emitted_right) / right_candidates.size();
        out_stats->actions_per_minute = source_duration > 0.0 ?
            emitted_right * 60.0 / source_duration : 0.0;
        out_stats->joint_strain_p95 = joint_strain.p95;
        out_stats->joint_strain_peak = joint_strain.peak;
        out_stats->minimum_right_gap_seconds = minimum_seconds;
        out_stats->minimum_right_gap_beats = minimum_beats;
        out_stats->audio_alignment_seconds = audio_alignment_seconds;
        out_stats->audio_offset_seconds = audio_offset_seconds;
        out_stats->alignment_confidence = estimated_alignment.confidence;
    }
    if (out_notes->size() > chart_row_limit) {
        const std::size_t complete_rows = out_notes->size();
        out_notes->clear();
        result.status = Status::error(StatusCode::ChartRowLimitExceeded,
            "complete generated chart requires " + std::to_string(complete_rows) +
            " rows, above the effective parser limit of " + std::to_string(chart_row_limit));
        return result;
    }
    SongConfig final_config = config;
    final_config.bpm = chart_bpm;
    final_config.notes = *out_notes;
    CompiledChart compiled;
    DiagnosticChartRetention tail;
    result.status = compile_chart(final_config, &compiled, &tail, chart_row_limit, request.native_assets);
    if (!result.status.ok()) out_notes->clear();
    return result;
}

} // namespace ff7rp::pipeline
