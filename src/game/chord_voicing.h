#pragma once
#include "game/chord_voicing_binding.h"
#include "game/module_hooks.h"

namespace ff7r::piano::game {
using ChordNameResolver = bool (*)(const std::string&, uint64_t&);
std::shared_ptr<ChordVoicingBinding> preflight_chord_voicing(
    const SelectionSnapshot&, void* wrapper, ChordNameResolver);
void finish_chord_voicing_expansion(void* wrapper, bool complete) noexcept;
bool chord_voicing_chart_update_allowed(void* wrapper) noexcept;
bool install_chord_voicing_hooks(const HookInstallContext&);
core::HookShutdownResult shutdown_chord_voicing();
#ifdef FF7RP_CHORD_VOICING_SELFTEST
bool chord_voicing_scope_selftest();
bool chord_voicing_callback_selftest(void* owner, void* event, bool charted, bool expect_original);
uintptr_t chord_voicing_bridge_selftest_scope(const PlaybackSnapshot&, void* owner,
    void* event, uintptr_t caller, uintptr_t vtable, uintptr_t entry,
    void (*probe)(void*, void*, void*), void* source, void* result);
#endif

#pragma pack(push, 1)
struct ChordVoice { uint64_t name; float velocity; };
#pragma pack(pop)
struct ChordProjection {
    std::array<uint8_t, 0x28> unused{};
    ChordVoice* data = nullptr;
    int32_t count = 0;
    int32_t capacity = 0;
    std::array<ChordVoice, 5> voices{};
};
static_assert(sizeof(ChordVoice) == 12);
static_assert(offsetof(ChordProjection, data) == 0x28);
static_assert(offsetof(ChordProjection, count) == 0x30);

inline bool project_chord_slots(const ResolvedChordVoicing& authored,
    const ChordVoice* stock, uint32_t width, ChordProjection& out) noexcept
{
    if (!stock || !authored.count || authored.count > 5 || width > 5
        || width != authored.stock_width || authored.count > width) return false;
    out.data = out.voices.data();
    out.count = out.capacity = static_cast<int32_t>(authored.count);
    for (uint32_t i = 0; i < authored.count; ++i)
        out.voices[i] = {authored.sounds[i], stock[i].velocity};
    return true;
}
} // namespace ff7r::piano::game
