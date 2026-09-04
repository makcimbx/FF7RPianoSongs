#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7rp::pipeline {

struct NativeChordConstituents {
    std::string_view chord_id;
    std::array<std::string_view, 4> sound_names;
    std::uint8_t sound_count;
};

// Offline evidence joins native PianoChordsAssign identities to their complete
// PianoChordsConfig rows. These are exact case-sensitive FName spellings and
// native voicings, not pitch aliases.
inline constexpr std::array<NativeChordConstituents, 64> kVerifiedNativeChordConstituents{{
    {"pca_C",        {"Cn2", "En2", "Gn2", ""}, 3},
    {"pca_C_m",      {"Cn2", "Ds2", "Gn2", ""}, 3},
    {"pca_C_dim",    {"Cn2", "Ds2", "Fs2", ""}, 3},
    {"pca_C_sus4",   {"Cn2", "Fn2", "Gn2", ""}, 3},
    {"pca_C_7",      {"Cn2", "En2", "Gn2", "As2"}, 4},
    {"pca_C_m7",     {"Cn2", "Ds2", "Gn2", "As2"}, 4},
    {"pca_C_Maj7",   {"Cn2", "En2", "Gn2", "Bn2"}, 4},
    {"pca_C_9",      {"Cn2", "En2", "Gn2", "Dn3"}, 4},
    {"pca_Cs",       {"Cs2", "Fn2", "Gs2", ""}, 3},
    {"pca_Db",       {"Db2", "Fn2", "Ab2", ""}, 3},
    {"pca_Cs_m",     {"Cs2", "En2", "Gs2", ""}, 3},
    {"pca_Db_sus4",  {"Db2", "Gb2", "Ab2", ""}, 3},
    {"pca_Db_Maj7",  {"Db2", "Fn2", "Ab2", "Cn3"}, 4},
    {"pca_D",        {"Dn2", "Fs2", "An2", ""}, 3},
    {"pca_D_m",      {"Dn2", "Fn2", "An2", ""}, 3},
    {"pca_D_dim",    {"Dn2", "Fn2", "Gs2", ""}, 3},
    {"pca_D_sus4",   {"Dn2", "Gn2", "An2", ""}, 3},
    {"pca_D_m7",     {"Dn2", "Fn2", "An2", "Cn3"}, 4},
    {"pca_D_9",      {"Dn2", "Fs2", "An2", "En3"}, 4},
    {"pca_D_m9",     {"Dn2", "Fn2", "An2", "En3"}, 4},
    {"pca_Eb",       {"Eb2", "Gn2", "Bb2", ""}, 3},
    {"pca_Eb_dim",   {"Eb2", "Gb2", "An2", ""}, 3},
    {"pca_Eb_sus4",  {"Eb2", "Ab2", "Bb2", ""}, 3},
    {"pca_Eb_m7",    {"Eb2", "Gb2", "Bb2", "Db3"}, 4},
    {"pca_Eb_9",     {"Eb2", "Gn2", "Bb2", "Fn3"}, 4},
    {"pca_Eb_mM7",   {"Eb2", "Gb2", "Bb2", "Dn3"}, 4},
    {"pca_E",        {"En2", "Gs2", "Bn2", ""}, 3},
    {"pca_E_m",      {"En2", "Gn2", "Bn2", ""}, 3},
    {"pca_E_dim",    {"En2", "Gn2", "As2", ""}, 3},
    {"pca_E_sus4",   {"En2", "An2", "Bn2", ""}, 3},
    {"pca_F",        {"Fn2", "An2", "Cn3", ""}, 3},
    {"pca_F_m",      {"Fn2", "Gs2", "Cn3", ""}, 3},
    {"pca_F_sus4",   {"Fn2", "As2", "Cn3", ""}, 3},
    {"pca_F_7",      {"Fn2", "An2", "Cn3", "Ds3"}, 4},
    {"pca_F_m7",     {"Fn2", "Gs2", "Cn3", "Ds3"}, 4},
    {"pca_F_9",      {"Fn2", "An2", "Cn3", "Gn3"}, 4},
    {"pca_Fs",       {"Fs2", "As2", "Cs3", ""}, 3},
    {"pca_Fs_dim",   {"Fs2", "An2", "Cn3", ""}, 3},
    {"pca_Fs_7",     {"Fs2", "As2", "Cs3", "En3"}, 4},
    {"pca_Gb_Maj7",  {"Gb2", "Bb2", "Db3", "Fn3"}, 4},
    {"pca_G",        {"Gn2", "Bn2", "Dn3", ""}, 3},
    {"pca_G_m",      {"Gn2", "As2", "Dn3", ""}, 3},
    {"pca_G_dim",    {"Gn2", "As2", "Cs3", ""}, 3},
    {"pca_G_sus4",   {"Gn2", "Cn3", "Dn3", ""}, 3},
    {"pca_G_7",      {"Gn2", "Bn2", "Dn3", "Fn3"}, 4},
    {"pca_G_Maj7",   {"Gn2", "Bn2", "Dn3", "Fs3"}, 4},
    {"pca_G_9",      {"Gn2", "Bn2", "Dn3", "An3"}, 4},
    {"pca_G_mM7",    {"Gn2", "As2", "Dn3", "Fs3"}, 4},
    {"pca_Ab",       {"Ab2", "Cn3", "Eb3", ""}, 3},
    {"pca_Ab_m",     {"Ab2", "Bn2", "Eb3", ""}, 3},
    {"pca_Ab_dim",   {"Ab2", "Bn2", "Dn3", ""}, 3},
    {"pca_A",        {"An2", "Cs3", "En3", ""}, 3},
    {"pca_A_m",      {"An2", "Cn3", "En3", ""}, 3},
    {"pca_A_sus4",   {"An2", "Dn3", "En3", ""}, 3},
    {"pca_A_m7",     {"An2", "Cn3", "En3", "Gn3"}, 4},
    {"pca_A_9",      {"An2", "Cs3", "En3", "Bn3"}, 4},
    {"pca_Bb",       {"Bb2", "Dn3", "Fn3", ""}, 3},
    {"pca_Bb_m",     {"Bb2", "Db3", "Fn3", ""}, 3},
    {"pca_Bb_dim",   {"Bb2", "Db3", "En3", ""}, 3},
    {"pca_Bb_sus4",  {"Bb2", "Eb3", "Fn3", ""}, 3},
    {"pca_Bb_m7",    {"Bb2", "Db3", "Fn3", "Ab3"}, 4},
    {"pca_B_m",      {"Bn2", "Dn3", "Fs3", ""}, 3},
    {"pca_B_dim",    {"Bn2", "Dn3", "Fn3", ""}, 3},
    {"pca_B_sus4",   {"Bn2", "En3", "Fs3", ""}, 3},
}};

constexpr const NativeChordConstituents* find_verified_native_chord(const std::string_view chord_id) {
    for (const auto& chord : kVerifiedNativeChordConstituents) {
        if (chord.chord_id == chord_id) return &chord;
    }
    return nullptr;
}

} // namespace ff7rp::pipeline
