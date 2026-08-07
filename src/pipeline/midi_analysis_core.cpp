#include "midi_analysis_core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace ff7rp::pipeline {
namespace {

constexpr double kComparisonEpsilon = 1e-9;

} // namespace

double tempo_at_tick(const std::vector<MidiTempoChange>& changes, const int tick) {
    auto position = std::upper_bound(changes.begin(), changes.end(), tick,
        [](const int value, const MidiTempoChange& change) { return value < change.tick; });
    if (position == changes.begin()) return 120.0;
    return std::prev(position)->bpm;
}

double humanization_window_ticks(
    const int ticks_per_quarter,
    const std::vector<MidiTempoChange>& tempos,
    const int anchor_tick) {
    const double bpm = tempo_at_tick(tempos, anchor_tick);
    const double twenty_ms_ticks = ticks_per_quarter * bpm * 0.020 / 60.0;
    return std::max(1.0, std::min(ticks_per_quarter / 32.0, twenty_ms_ticks));
}

double metric_accent_at_tick(
    const int tick,
    const int ticks_per_quarter,
    const std::vector<MidiMeterChange>& meters) {
    auto position = std::upper_bound(meters.begin(), meters.end(), tick,
        [](const int value, const MidiMeterChange& change) { return value < change.tick; });
    const MidiMeterChange& meter = position == meters.begin() ? meters.front() : *std::prev(position);
    const long long beat_units = static_cast<long long>(ticks_per_quarter) * 4;
    const long long measure_units = beat_units * meter.numerator;
    if (beat_units <= 0 || measure_units <= 0) return 0.0;
    const long long relative_units = static_cast<long long>(std::max(0, tick - meter.tick)) *
        meter.denominator;
    const long long phase = relative_units % measure_units;
    if (phase == 0) return 1.0;
    if (phase % beat_units == 0) return 0.55;
    if ((phase * 2) % beat_units == 0) return 0.20;
    return 0.0;
}

std::vector<OnsetCluster> build_onset_clusters(
    const std::vector<NormalizedMidiNoteEvent>& source,
    const int ticks_per_quarter,
    const std::vector<MidiTempoChange>& tempos,
    const std::vector<MidiMeterChange>& meters,
    std::size_t* exact_group_count,
    std::size_t* humanized_event_count) {
    std::vector<NormalizedMidiNoteEvent> sorted = source;
    std::sort(sorted.begin(), sorted.end(),
        [](const NormalizedMidiNoteEvent& a, const NormalizedMidiNoteEvent& b) {
            return a.source < b.source;
        });
    std::vector<std::vector<NormalizedMidiNoteEvent>> exact_groups;
    for (const NormalizedMidiNoteEvent& note : sorted) {
        if (exact_groups.empty() || exact_groups.back().front().source.tick != note.source.tick) {
            exact_groups.emplace_back();
        }
        exact_groups.back().push_back(note);
    }
    if (exact_group_count) *exact_group_count = exact_groups.size();

    std::vector<OnsetCluster> clusters;
    std::size_t folded = 0;
    for (std::size_t begin = 0; begin < exact_groups.size();) {
        const int anchor_tick = exact_groups[begin].front().source.tick;
        const double window = humanization_window_ticks(ticks_per_quarter, tempos, anchor_tick);
        std::size_t end = begin + 1;
        while (end < exact_groups.size() &&
            exact_groups[end].front().source.tick - anchor_tick <= window + kComparisonEpsilon) {
            folded += exact_groups[end].size();
            ++end;
        }
        OnsetCluster cluster;
        cluster.anchor_tick = anchor_tick;
        cluster.start = exact_groups[begin].front().start;
        cluster.beat = static_cast<double>(anchor_tick) / ticks_per_quarter;
        cluster.metric_accent = metric_accent_at_tick(anchor_tick, ticks_per_quarter, meters);
        for (std::size_t i = begin; i < end; ++i) {
            cluster.notes.insert(cluster.notes.end(), exact_groups[i].begin(), exact_groups[i].end());
        }
        std::sort(cluster.notes.begin(), cluster.notes.end(),
            [](const NormalizedMidiNoteEvent& a, const NormalizedMidiNoteEvent& b) {
                return a.source < b.source;
            });
        clusters.push_back(std::move(cluster));
        begin = end;
    }
    if (humanized_event_count) *humanized_event_count = folded;
    return clusters;
}

std::vector<Attack> build_alignment_attacks(const std::vector<OnsetCluster>& clusters) {
    std::vector<Attack> attacks;
    attacks.reserve(clusters.size());
    for (const OnsetCluster& cluster : clusters) {
        if (cluster.notes.empty()) continue;
        const NormalizedMidiNoteEvent* strongest = &cluster.notes.front();
        for (const NormalizedMidiNoteEvent& note : cluster.notes) {
            const auto candidate = std::make_tuple(note.source.velocity, note.stream_prior,
                note.source.pitch, note.end - note.start);
            const auto current = std::make_tuple(strongest->source.velocity, strongest->stream_prior,
                strongest->source.pitch, strongest->end - strongest->start);
            if (candidate > current) strongest = &note;
        }
        Attack attack;
        attack.event = *strongest;
        attack.start = strongest->start;
        attack.end = strongest->end;
        attack.beat = cluster.beat;
        attack.metric_accent = cluster.metric_accent;
        attacks.push_back(std::move(attack));
    }
    return attacks;
}

namespace midi_melody_scoring_detail {

double voice_emission(
    const int pitch_value,
    const int velocity_value,
    const double start,
    const double end,
    const double stream_prior,
    const double metric_accent) {
    const double pitch = std::clamp((pitch_value - 36.0) / 60.0, 0.0, 1.0);
    const double velocity = velocity_value / 127.0;
    const double duration = std::clamp(end - start, 0.0, 1.0);
    return 1.15 * pitch + 0.95 * velocity + 0.75 * stream_prior +
        0.25 * duration + 0.20 * metric_accent;
}

double melody_evidence(
    const int pitch_value,
    const int velocity_value,
    const double start,
    const double end,
    const double stream_prior) {
    const double pitch = std::clamp((pitch_value - 36.0) / 60.0, 0.0, 1.0);
    const double velocity = velocity_value / 127.0;
    const double duration = std::clamp((end - start) / 0.75, 0.0, 1.0);
    return 0.30 * pitch + 0.28 * velocity + 0.30 * stream_prior + 0.12 * duration;
}

} // namespace midi_melody_scoring_detail

namespace {

using MidiNoteEvent = NormalizedMidiNoteEvent;

int direction_index(const int movement) {
    if (movement < 0) return 0;
    if (movement > 0) return 2;
    return 1;
}

struct VoiceState {
    double score = -std::numeric_limits<double>::infinity();
    int previous_node = -1;
    int previous_direction = -1;
};

struct VoiceNode {
    std::size_t cluster = 0;
    std::size_t candidate = 0;
    std::array<VoiceState, 3> states;
};

std::vector<Attack> track_melody_voice(
    const std::vector<OnsetCluster>& clusters,
    std::size_t* stream_changes) {
    if (clusters.empty()) return {};
    std::vector<VoiceNode> nodes;
    for (std::size_t cluster = 0; cluster < clusters.size(); ++cluster) {
        for (std::size_t candidate = 0; candidate < clusters[cluster].notes.size(); ++candidate) {
            VoiceNode node;
            node.cluster = cluster;
            node.candidate = candidate;
            nodes.push_back(std::move(node));
        }
    }
    for (std::size_t current = 0; current < nodes.size(); ++current) {
        VoiceNode& current_node = nodes[current];
        const OnsetCluster& current_cluster = clusters[current_node.cluster];
        const MidiNoteEvent& current_note = current_cluster.notes[current_node.candidate];
        const double emission = midi_melody_scoring_detail::voice_emission(
            current_note.source.pitch,
            current_note.source.velocity,
            current_note.start,
            current_note.end,
            current_note.stream_prior,
            current_cluster.metric_accent);
        if (current_node.cluster == 0) {
            current_node.states[1].score = emission;
            continue;
        }
        for (std::size_t reverse = current; reverse > 0; --reverse) {
            const std::size_t previous_index = reverse - 1;
            const VoiceNode& previous_node = nodes[previous_index];
            if (previous_node.cluster >= current_node.cluster) continue;
            if (current_node.cluster - previous_node.cluster > 64) break;
            const MidiNoteEvent& previous_note =
                clusters[previous_node.cluster].notes[previous_node.candidate];
            const int movement = current_note.source.pitch - previous_note.source.pitch;
            const int next_direction = direction_index(movement);
            const bool same_stream = current_note.source.track == previous_note.source.track &&
                current_note.source.channel == previous_note.source.channel;
            double transition = same_stream ? 0.55 : -0.70;
            transition -= 0.10 * std::abs(movement);
            if (std::abs(movement) > 12) transition -= 0.10 * (std::abs(movement) - 12);
            transition -= 0.18 * static_cast<double>(
                current_node.cluster - previous_node.cluster - 1);
            if (previous_note.source.end_tick >= current_note.source.tick) transition += 0.20;
            for (int old_direction = 0; old_direction < 3; ++old_direction) {
                const VoiceState& old_state = previous_node.states[old_direction];
                if (!std::isfinite(old_state.score)) continue;
                double direction_penalty = 0.0;
                if (old_direction != 1 && next_direction != 1 && old_direction != next_direction) {
                    direction_penalty = 0.85;
                }
                const double score = old_state.score + transition - direction_penalty + emission;
                VoiceState& destination = current_node.states[next_direction];
                if (score > destination.score + kComparisonEpsilon) {
                    destination.score = score;
                    destination.previous_node = static_cast<int>(previous_index);
                    destination.previous_direction = old_direction;
                }
            }
        }
    }

    int node_index = -1;
    int direction = 1;
    double best = -std::numeric_limits<double>::infinity();
    const std::size_t last_cluster = clusters.size() - 1;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].cluster != last_cluster) continue;
        for (int d = 0; d < 3; ++d) {
            if (nodes[i].states[d].score > best + kComparisonEpsilon) {
                best = nodes[i].states[d].score;
                node_index = static_cast<int>(i);
                direction = d;
            }
        }
    }
    std::vector<int> path;
    while (node_index >= 0) {
        path.push_back(node_index);
        const VoiceState& state = nodes[static_cast<std::size_t>(node_index)].states[direction];
        node_index = state.previous_node;
        direction = state.previous_direction;
    }
    std::reverse(path.begin(), path.end());

    std::vector<Attack> voice;
    voice.reserve(path.size());
    std::size_t changes = 0;
    for (const int selected_node : path) {
        const VoiceNode& node = nodes[static_cast<std::size_t>(selected_node)];
        const OnsetCluster& cluster = clusters[node.cluster];
        const MidiNoteEvent& note = cluster.notes[node.candidate];
        Attack attack;
        attack.event = note;
        attack.start = cluster.start;
        attack.end = std::max(note.end, attack.start);
        attack.beat = cluster.beat;
        attack.metric_accent = cluster.metric_accent;
        attack.melody_evidence = midi_melody_scoring_detail::melody_evidence(
            note.source.pitch,
            note.source.velocity,
            note.start,
            note.end,
            note.stream_prior);
        if (!voice.empty() && (voice.back().event.source.track != note.source.track ||
            voice.back().event.source.channel != note.source.channel)) {
            ++changes;
        }
        voice.push_back(std::move(attack));
    }
    if (stream_changes) *stream_changes = changes;
    return voice;
}

constexpr double kAudioWindowSeconds = 0.055;
constexpr double kAlignmentWindowSeconds = 0.055;
constexpr double kAutoAlignmentRangeSeconds = 1.0;

double tone_magnitude(
    const WavAudio& audio,
    const std::size_t begin_frame,
    const std::size_t frame_count,
    const double frequency) {
    if (frame_count < 2 || begin_frame + frame_count > audio.frame_count()) return 0.0;
    const double angular = -2.0 * 3.14159265358979323846 * frequency / audio.sample_rate;
    const std::complex<double> step{std::cos(angular), std::sin(angular)};
    std::complex<double> oscillator{1.0, 0.0};
    std::complex<double> sum{0.0, 0.0};
    for (std::size_t i = 0; i < frame_count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * 3.14159265358979323846 * i / (frame_count - 1));
        const std::size_t sample = (begin_frame + i) * 2;
        const double mono = (audio.stereo_samples[sample] + audio.stereo_samples[sample + 1]) * 0.5;
        sum += oscillator * (mono * window);
        oscillator *= step;
    }
    return std::abs(sum);
}

double audio_novelty(
    const WavAudio& audio,
    const Attack& attack,
    const double alignment_seconds,
    const double window_seconds,
    const std::size_t harmonic_count) {
    if (audio.sample_rate == 0 || audio.channels != 2 || harmonic_count == 0) return 0.0;
    const std::size_t window_frames = static_cast<std::size_t>(
        std::llround(window_seconds * audio.sample_rate));
    const long long center = std::llround((attack.start + alignment_seconds) * audio.sample_rate);
    if (center < static_cast<long long>(window_frames) ||
        center + static_cast<long long>(window_frames) > static_cast<long long>(audio.frame_count())) {
        return 0.0;
    }
    const double fundamental = 440.0 * std::pow(2.0, (attack.event.source.pitch - 69) / 12.0);
    constexpr std::array<double, 3> weights{1.0, 0.45, 0.25};
    double score = 0.0;
    for (std::size_t harmonic = 1; harmonic <= std::min(harmonic_count, weights.size()); ++harmonic) {
        const double frequency = fundamental * harmonic;
        const double before = tone_magnitude(audio,
            static_cast<std::size_t>(center) - window_frames, window_frames, frequency);
        const double after = tone_magnitude(audio,
            static_cast<std::size_t>(center), window_frames, frequency);
        score += weights[harmonic - 1] * std::max(0.0, after - before);
    }
    return score;
}

} // namespace

MidiMelodyTrackingResult track_midi_melody_voice(
    const std::vector<OnsetCluster>& clusters) {
    MidiMelodyTrackingResult result;
    result.attacks = track_melody_voice(clusters, &result.stream_changes);
    return result;
}

MidiAudioAlignmentResult estimate_midi_audio_alignment(
    const WavAudio& audio,
    const std::vector<Attack>& attacks) {
    MidiAudioAlignmentResult result;
    if (attacks.empty() || audio.frame_count() == 0 || audio.sample_rate == 0 || audio.channels != 2) {
        return result;
    }
    constexpr std::size_t kAnchorCount = 192;
    const std::size_t count = std::min(kAnchorCount, attacks.size());
    std::vector<const Attack*> anchors;
    anchors.reserve(count);
    for (std::size_t bin = 0; bin < count; ++bin) {
        const std::size_t begin = bin * attacks.size() / count;
        const std::size_t end = std::max(begin + 1, (bin + 1) * attacks.size() / count);
        const Attack* strongest = &attacks[begin];
        for (std::size_t index = begin + 1; index < std::min(end, attacks.size()); ++index) {
            if (attacks[index].event.source.velocity > strongest->event.source.velocity) {
                strongest = &attacks[index];
            }
        }
        anchors.push_back(strongest);
    }
    const auto score_at = [&audio, &anchors](const double offset) {
        double score = 0.0;
        for (const Attack* attack : anchors) {
            score += std::log1p(audio_novelty(audio, *attack, offset, kAlignmentWindowSeconds, 3));
        }
        return score;
    };
    double best_offset = 0.0;
    double best_score = -1.0;
    std::vector<std::pair<double, double>> coarse;
    for (int millisecond = -1000; millisecond <= 1000; millisecond += 10) {
        const double offset = millisecond / 1000.0;
        const double score = score_at(offset);
        coarse.emplace_back(offset, score);
        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }
    const double coarse_best = best_offset;
    for (int delta_ms = -15; delta_ms <= 15; ++delta_ms) {
        const double offset = std::clamp(coarse_best + delta_ms / 1000.0,
            -kAutoAlignmentRangeSeconds, kAutoAlignmentRangeSeconds);
        const double score = score_at(offset);
        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }
    double competing_score = 0.0;
    for (const auto& candidate : coarse) {
        if (std::fabs(candidate.first - best_offset) >= 0.04) {
            competing_score = std::max(competing_score, candidate.second);
        }
    }
    result.seconds = best_score > 0.0 ?
        std::clamp(best_offset - kAlignmentWindowSeconds * 0.5,
            -kAutoAlignmentRangeSeconds, kAutoAlignmentRangeSeconds) : 0.0;
    result.confidence = best_score > 0.0 ?
        std::clamp((best_score - competing_score) / best_score, 0.0, 1.0) : 0.0;
    return result;
}

double measure_midi_attack_audio_novelty(
    const WavAudio& audio,
    const Attack& attack,
    const double alignment_seconds) {
    return audio_novelty(audio, attack, alignment_seconds, kAudioWindowSeconds, 3);
}

} // namespace ff7rp::pipeline
