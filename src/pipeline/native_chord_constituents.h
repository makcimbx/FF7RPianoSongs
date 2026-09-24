#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "native_asset_capabilities.h"

namespace ff7rp::pipeline {

struct NativeChordConstituents {
    std::string_view chord_id;
    std::array<std::string_view, 4> sound_names;
    std::uint8_t sound_count;
};

// Offline evidence joins native PianoChordsAssign identities to their complete
// PianoChordsConfig rows. These are exact case-sensitive FName spellings and
// native voicings, not pitch aliases. Complete 170-row assignment/config join;
// the original 64 entries retain their order. Comments on added rows preserve
// exact config keys (not derived from assignment spelling). Stock velocities
// remain runtime-owned and are not normalized or synthesized here.
inline constexpr std::array<NativeChordConstituents, 170> kVerifiedNativeChordConstituents{{
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
    {"pca_A_7", {"An2", "Cs3", "En3", "Gn3"}, 4}, // A7
    {"pca_A_Maj7", {"An2", "Cs3", "En3", "Gs3"}, 4}, // AMaj7
    {"pca_A_dim", {"An2", "Cn3", "Ds3", ""}, 3}, // Adim
    {"pca_A_m9", {"An2", "Cn3", "En3", "Bn3"}, 4}, // Am9
    {"pca_A_mM7", {"An2", "Cn3", "En3", "Gs3"}, 4}, // AmM7
    {"pca_Ab_7", {"Ab2", "Cn3", "Eb3", "Gb3"}, 4}, // Ab7
    {"pca_Ab_9", {"Ab2", "Cn3", "Eb3", "Bb3"}, 4}, // Ab9
    {"pca_Ab_Maj7", {"Ab2", "Cn3", "Eb3", "Gn3"}, 4}, // AbMaj7
    {"pca_Ab_m7", {"Ab2", "Bn2", "Eb3", "Gb3"}, 4}, // Abm7
    {"pca_Ab_m9", {"Ab2", "Bn2", "Eb3", "Bb3"}, 4}, // Abm9
    {"pca_Ab_mM7", {"Ab2", "Bn2", "Eb3", "Gn3"}, 4}, // AbmM7
    {"pca_Ab_sus4", {"Ab2", "Db3", "Eb3", ""}, 3}, // Absus4
    {"pca_As", {"As2", "Dn3", "Fn3", ""}, 3}, // As
    {"pca_As_7", {"As2", "Dn3", "Fn3", "Gs3"}, 4}, // As7
    {"pca_As_9", {"As2", "Dn3", "Fn3", "Cn3"}, 4}, // As9
    {"pca_As_Maj7", {"As2", "Dn3", "Fn3", "An3"}, 4}, // AsMaj7
    {"pca_As_dim", {"As2", "Cs3", "En3", ""}, 3}, // Asdim
    {"pca_As_m", {"As2", "Cs3", "Fn3", ""}, 3}, // Asm
    {"pca_As_m7", {"As2", "Cs3", "Fn3", "Gs3"}, 4}, // Asm7
    {"pca_As_m9", {"As2", "Cs3", "Fn3", "Cn3"}, 4}, // Asm9
    {"pca_As_mM7", {"As2", "Cs3", "Fn3", "An3"}, 4}, // AsmM7
    {"pca_As_sus4", {"As2", "Ds3", "Fn3", ""}, 3}, // Assus4
    {"pca_B", {"Bn2", "Ds3", "Fs3", ""}, 3}, // B
    {"pca_B_7", {"Bn2", "Ds3", "Fs3", "An3"}, 4}, // B7
    {"pca_B_9", {"Bn2", "Ds3", "Fs3", "Cs3"}, 4}, // B9
    {"pca_B_Maj7", {"Bn2", "Ds3", "Fs3", "As3"}, 4}, // BMaj7
    {"pca_B_m7", {"Bn2", "Dn3", "Fs3", "An3"}, 4}, // Bm7
    {"pca_B_m9", {"Bn2", "Dn3", "Fs3", "Cs3"}, 4}, // Bm9
    {"pca_B_mM7", {"Bn2", "Dn3", "Fs3", "As3"}, 4}, // BmM7
    {"pca_Bb_7", {"Bb2", "Dn3", "Fn3", "Ab3"}, 4}, // Bb7
    {"pca_Bb_9", {"Bb2", "Dn3", "Fn3", "Cn3"}, 4}, // Bb9
    {"pca_Bb_Maj7", {"Bb2", "Dn3", "Fn3", "An3"}, 4}, // BbMaj7
    {"pca_Bb_m9", {"Bb2", "Db3", "Fn3", "Cn3"}, 4}, // Bbm9
    {"pca_Bb_mM7", {"Bb2", "Db3", "Fn3", "An3"}, 4}, // BbmM7
    {"pca_C_m9", {"Cn2", "Ds2", "Gn2", "Dn3"}, 4}, // Cm9
    {"pca_C_mM7", {"Cn2", "Ds2", "Gn2", "Bn2"}, 4}, // CmM7
    {"pca_Cs_7", {"Cs2", "Fn2", "Gs2", "Bn2"}, 4}, // Cs7
    {"pca_Cs_9", {"Cs2", "Fn2", "Gs2", "Ds3"}, 4}, // Cs9
    {"pca_Cs_Maj7", {"Cs2", "Fn2", "Gs2", "Cn3"}, 4}, // CsMaj7
    {"pca_Cs_dim", {"Cs2", "En2", "Gn2", ""}, 3}, // Csdim
    {"pca_Cs_m7", {"Cs2", "En2", "Gs2", "Bn2"}, 4}, // Csm7
    {"pca_Cs_m9", {"Cs2", "En2", "Gs2", "Ds3"}, 4}, // Csm9
    {"pca_Cs_mM7", {"Cs2", "En2", "Gs2", "Cn3"}, 4}, // CsmM7
    {"pca_Cs_sus4", {"Cs2", "Fs2", "Gs2", ""}, 3}, // Cssus4
    {"pca_D_7", {"Dn2", "Fs2", "An2", "Cn3"}, 4}, // D7
    {"pca_D_Maj7", {"Dn2", "Fs2", "An2", "Cs3"}, 4}, // DMaj7
    {"pca_D_mM7", {"Dn2", "Fn2", "An2", "Cs3"}, 4}, // DmM7
    {"pca_Db_7", {"Db2", "Fn2", "Ab2", "Bn2"}, 4}, // Db7
    {"pca_Db_9", {"Db2", "Fn2", "Ab2", "Eb3"}, 4}, // Db9
    {"pca_Db_dim", {"Db2", "En2", "Gn2", ""}, 3}, // Dbdim
    {"pca_Db_m", {"Db2", "En2", "Ab2", ""}, 3}, // Dbm
    {"pca_Db_m7", {"Db2", "En2", "Ab2", "Bn2"}, 4}, // Dbm7
    {"pca_Db_m9", {"Db2", "En2", "Ab2", "Eb3"}, 4}, // Dbm9
    {"pca_Db_mM7", {"Db2", "En2", "Ab2", "Cn3"}, 4}, // DbmM7
    {"pca_Ds", {"Ds2", "Gn2", "As2", ""}, 3}, // Ds
    {"pca_Ds_7", {"Ds2", "Gn2", "As2", "Cs3"}, 4}, // Ds7
    {"pca_Ds_9", {"Ds2", "Gn2", "As2", "Fn3"}, 4}, // Ds9
    {"pca_Ds_Maj7", {"Ds2", "Gn2", "As2", "Dn3"}, 4}, // DsMaj7
    {"pca_Ds_dim", {"Ds2", "Fs2", "An2", ""}, 3}, // Dsdim
    {"pca_Ds_m", {"Ds2", "Fs2", "As2", ""}, 3}, // Dsm
    {"pca_Ds_m7", {"Ds2", "Fs2", "As2", "Cs3"}, 4}, // Dsm7
    {"pca_Ds_m9", {"Ds2", "Fs2", "As2", "Fn3"}, 4}, // Dsm9
    {"pca_Ds_mM7", {"Ds2", "Fs2", "As2", "Dn3"}, 4}, // DsmM7
    {"pca_Ds_sus4", {"Ds2", "Gs2", "As2", ""}, 3}, // Dssus4
    {"pca_E_7", {"En2", "Gs2", "Bn2", "Dn3"}, 4}, // E7
    {"pca_E_9", {"En2", "Gs2", "Bn2", "Fs3"}, 4}, // E9
    {"pca_E_Maj7", {"En2", "Gs2", "Bn2", "Ds3"}, 4}, // EMaj7
    {"pca_E_m7", {"En2", "Gn2", "Bn2", "Dn3"}, 4}, // Em7
    {"pca_E_m9", {"En2", "Gn2", "Bn2", "Fs3"}, 4}, // Em9
    {"pca_E_mM7", {"En2", "Gn2", "Bn2", "Ds3"}, 4}, // EmM7
    {"pca_Eb_7", {"Eb2", "Gn2", "Bb2", "Db3"}, 4}, // Eb7
    {"pca_Eb_Maj7", {"Eb2", "Gn2", "Bb2", "Dn3"}, 4}, // EbMaj7
    {"pca_Eb_m", {"Eb2", "Gb2", "Bb2", ""}, 3}, // Ebm
    {"pca_Eb_m9", {"Eb2", "Gb2", "Bb2", "Fn3"}, 4}, // Ebm9
    {"pca_F_Maj7", {"Fn2", "An2", "Cn3", "En3"}, 4}, // FMaj7
    {"pca_F_dim", {"Fn2", "Gs2", "Bn2", ""}, 3}, // Fdim
    {"pca_F_m9", {"Fn2", "Gs2", "Cn3", "Gn3"}, 4}, // Fm9
    {"pca_F_mM7", {"Fn2", "Gs2", "Cn3", "En3"}, 4}, // FmM7
    {"pca_Fs_9", {"Fs2", "As2", "Cs3", "Gs3"}, 4}, // Fs9
    {"pca_Fs_Maj7", {"Fs2", "As2", "Cs3", "Fn3"}, 4}, // FsMaj7
    {"pca_Fs_m", {"Fs2", "An2", "Cs3", ""}, 3}, // Fsm
    {"pca_Fs_m7", {"Fs2", "An2", "Cs3", "En3"}, 4}, // Fsm7
    {"pca_Fs_m9", {"Fs2", "An2", "Cs3", "Gs3"}, 4}, // Fsm9
    {"pca_Fs_mM7", {"Fs2", "An2", "Cs3", "Fn3"}, 4}, // FsmM7
    {"pca_Fs_sus4", {"Fs2", "Bn2", "Cs3", ""}, 3}, // Fssus4
    {"pca_G_m7", {"Gn2", "As2", "Dn3", "Fn3"}, 4}, // Gm7
    {"pca_G_m9", {"Gn2", "As2", "Dn3", "An3"}, 4}, // Gm9
    {"pca_Gb", {"Gb2", "Bb2", "Db3", ""}, 3}, // Gb
    {"pca_Gb_7", {"Gb2", "Bb2", "Db3", "En3"}, 4}, // Gb7
    {"pca_Gb_9", {"Gb2", "Bb2", "Db3", "Ab3"}, 4}, // Gb9
    {"pca_Gb_dim", {"Gb2", "An2", "Cn3", ""}, 3}, // Gbdim
    {"pca_Gb_m", {"Gb2", "An2", "Db3", ""}, 3}, // Gbm
    {"pca_Gb_m7", {"Gb2", "An2", "Db3", "En3"}, 4}, // Gbm7
    {"pca_Gb_m9", {"Gb2", "An2", "Db3", "Ab3"}, 4}, // Gbm9
    {"pca_Gb_mM7", {"Gb2", "An2", "Db3", "Fn3"}, 4}, // GbmM7
    {"pca_Gb_sus4", {"Gb2", "Bn2", "Db3", ""}, 3}, // Gbsus4
    {"pca_Gs", {"Gs2", "Cn3", "Ds3", ""}, 3}, // GS
    {"pca_Gs_7", {"Gs2", "Cn3", "Ds3", "Fs3"}, 4}, // Gs7
    {"pca_Gs_9", {"Gs2", "Cn3", "Ds3", "As3"}, 4}, // Gs9
    {"pca_Gs_Maj7", {"Gs2", "Cn3", "Ds3", "Gn3"}, 4}, // GsMaj7
    {"pca_Gs_dim", {"Gs2", "Bn2", "Dn3", ""}, 3}, // Gsdim
    {"pca_Gs_m", {"Gs2", "Bn2", "Ds3", ""}, 3}, // Gsm
    {"pca_Gs_m7", {"Gs2", "Bn2", "Ds3", "Fs3"}, 4}, // Gsm7
    {"pca_Gs_m9", {"Gs2", "Bn2", "Ds3", "As3"}, 4}, // Gsm9
    {"pca_Gs_mM7", {"Gs2", "Bn2", "Ds3", "Gn3"}, 4}, // GsmM7
    {"pca_Gs_sus4", {"Gs2", "Cs3", "Ds3", ""}, 3}, // Gssus4
}};

constexpr const NativeChordConstituents* find_verified_native_chord(
    const std::string_view chord_id, const NativeAssetCapabilities capabilities) {
    if (chord_id == "pca_Db" && !capabilities.has_verified_pca_db_voicing()) return nullptr;
    for (const auto& chord : kVerifiedNativeChordConstituents) {
        if (chord.chord_id == chord_id) return &chord;
    }
    return nullptr;
}

} // namespace ff7rp::pipeline
