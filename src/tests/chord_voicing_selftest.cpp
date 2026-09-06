#include "game/chord_voicing.h"
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include <iostream>

using namespace ff7r::piano::game;
extern "C" {
extern void* ff7rp_chord_copy_original;
void ff7rp_chord_bridge_probe(void*, void*, void*);
void ff7rp_chord_probe_return();
void ff7rp_chord_copy_bridge();
void ff7rp_chord_copy_bridge_end();
}
namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() { static core::HookCallbackGate gate; return gate; }
void fail_chord_voicing_chart(std::shared_ptr<const ChordVoicingBinding> binding) noexcept {
    registry().fail_chart_admission(binding);
    const auto playback = registry().playback_snapshot();
    if (playback.chart_admission.binding == binding) registry().revoke_playback(playback.token);
}
}
namespace {
struct Probe { uintptr_t rax, rsi, before, after, preserved; };
void* expected_dst;
void* expected_src;
int calls = 0;
bool original_args = true, unwind_ok = true;

__declspec(noinline) void* fake_copy(void* dst, void* src)
{
    ++calls;
    original_args &= dst == expected_dst && src == expected_src;
    // Unwind through dispatch and the actual MASM .pdata/.xdata, not a model.
    CONTEXT context{};
    RtlCaptureContext(&context);
    bool saw_bridge = false;
    for (int depth = 0; depth < 16; ++depth) {
        DWORD64 base = 0, establisher = 0;
        void* data = nullptr;
        const bool in_bridge = context.Rip >= reinterpret_cast<DWORD64>(&ff7rp_chord_copy_bridge)
            && context.Rip < reinterpret_cast<DWORD64>(&ff7rp_chord_copy_bridge_end);
        const auto function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (!function) break;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function,
            &context, &data, &establisher, nullptr);
        if (in_bridge) {
            saw_bridge = context.Rip == reinterpret_cast<DWORD64>(&ff7rp_chord_probe_return)
                && context.Rsi == 0x11223344;
            break;
        }
    }
    unwind_ok &= saw_bridge;
    return dst;
}
template<class T, size_t N> void put(std::array<uint8_t, N>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
bool check(bool value, const char* label) {
    if (!value) std::cerr << "chord_voicing_selftest: " << label << '\n';
    return value;
}
}

int main()
{
    bool ok = true;
    auto& reg = registry();
    SongDescriptor song;
    song.id = "voicing"; song.visible_index = 12; song.profiles.resize(1);
    song.chord_voicings.push_back({"pca_C", {"Ab2", "Cn3", "En3"}});
    reg.replace({song}); reg.set_active_selection(12, 0);
    const auto selection = reg.selection_snapshot();
    CustomContextToken token{selection.generation, 1, 2, 3};
    std::array<uint8_t, 0x120> wrapper{};
    std::array<uint8_t, 0x1000> controller{};
    std::array<uint8_t, 0x200> owner{};
    std::array<uint8_t, 0x30> table{};
    std::array<uint8_t, 0x120> event{};
    std::array<uintptr_t, 2> selected_owner{reinterpret_cast<uintptr_t>(owner.data()), 1};
    constexpr uintptr_t entry = 0x1234;
    put(wrapper, 0x118, controller.data());
    put(controller, 0xf48, wrapper.data()); put(controller, 0xf50, owner.data());
    put(controller, 0x380, selected_owner.data()); put(controller, 0x388, int32_t{1});
    put(controller, 0x520, int32_t{0}); put(owner, 0, table.data()); put(table, 0x20, entry);
    auto binding = std::make_shared<ChordVoicingBinding>();
    binding->selection = selection; binding->lease = token; binding->wrapper = wrapper.data();
    binding->controller = controller.data(); binding->control_block = owner.data();
    binding->left = controller.data() + 0x380; binding->right = controller.data() + 0x538;
    ff7rp::pipeline::ChartEventRow row; row.time_str = "00_00"; row.chord_id = "pca_C";
    binding->plan = ff7rp::pipeline::derive_chart_event_plan({row});
    if (!check(binding->plan.valid() && binding->plan.events.size() == 1, "valid canonical fixture plan")) return 1;
    binding->event_names = {42}; binding->voicings = {{42, {101, 102, 103}, 3, 3}};
    binding->event_successors = {ff7rp::pipeline::kNoChartEvent};
    NativeChartHeader header{reinterpret_cast<uintptr_t>(event.data()), 1, 1};
    put(wrapper, 0x80, header); put(event, 0, wrapper.data()); put(event, 8, binding->left);
    put(event, 0x20, uint32_t{1}); put(event, 0x2c, uint64_t{42}); put(event, 0x49, uint8_t{1});
    ok &= check(reg.acquire_selection_guard(selection, token)
        && reg.attach_chart_admission(selection, token, binding), "preflight guard attach");
    ok &= check(reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::RejectedFirst
        && reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Rejected,
        "immediate pending update rejected, bounded diagnostic");
    ok &= check(reg.chart_update_admission(owner.data()) == ChartUpdateAdmission::Stock, "unrelated stock");
    ok &= check(reg.publish_playback_from_selection_guard(selection, token)
        && reg.playback_snapshot().chart_admission.binding == binding
        && reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Rejected,
        "nested publication transfers pending binding");
    finish_chord_voicing_expansion(wrapper.data(), true);
    ok &= check(reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Ready, "seal allows immediately");
    if (!ok) return 1;
    auto updated = token; ++updated.route_generation;
    ok &= check(reg.update_playback_token(token, updated), "same lease token update");
    ok &= check(reg.commit_if_current_chart(binding,
        +[](const ChartAdmissionSnapshot&, void*) noexcept { return true; }, nullptr), "updated token current");
    if (!ok) return 1;
    ok &= check(chord_voicing_scope_selftest(), "nested stock shadows eligible frame");

    std::array<ChordVoice, 3> stock{{{201, 75}, {202, 65}, {203, 70}}};
    ChordProjection projection;
    ok &= check(project_chord_slots(binding->voicings[0], stock.data(), 3, projection)
        && projection.voices[1].name == 102 && projection.voices[1].velocity == 65,
        "ordered new names inherit positional velocity");
    // Model the untouched native qword filter, including FName number bits.
    auto survivors = [&](std::initializer_list<uint64_t> ignored) {
        int count = 0;
        for (int i = 0; i < projection.count; ++i) {
            bool member = false;
            for (auto name : ignored) member |= name == projection.voices[i].name;
            if (!member) ++count;
        }
        return count;
    };
    // 101 is synthetic Ab2; 104 is distinct Gs2, never an enharmonic match.
    ok &= check(survivors({101}) == 2 && survivors({999}) == 3 && survivors({104}) == 3
        && survivors({101 | (uint64_t{1} << 32)}) == 3
        && survivors({101,102,103}) == 0 && survivors({201,202,203}) == 3,
        "exact/nonmember/number/zero/empty stock-filtered semantics");
    ok &= check(!project_chord_slots(binding->voicings[0], stock.data(), 2, projection), "native width invariant");

    std::array<uint8_t, 0x38> cached{};
    put(cached, 0, uint64_t{42});
    put(cached, 0x28, NativeChartHeader{reinterpret_cast<uintptr_t>(stock.data()),3,3});
    expected_dst = owner.data() + 0xf8; expected_src = cached.data();
    ff7rp_chord_copy_original = reinterpret_cast<void*>(&fake_copy);
    Probe probe{};
    ff7rp_chord_bridge_probe(expected_dst, expected_src, &probe);
    ok &= check(calls == 1 && original_args && unwind_ok && probe.rax == reinterpret_cast<uintptr_t>(expected_dst)
        && probe.rsi == 0x11223344 && probe.before == probe.after && probe.preserved, "stock ABI/unwind/original once");
    if (!ok) return 1;
    const auto projected = chord_voicing_bridge_selftest_scope(reg.playback_snapshot(), owner.data(), event.data(),
        reinterpret_cast<uintptr_t>(&ff7rp_chord_probe_return), reinterpret_cast<uintptr_t>(table.data()), entry,
        &ff7rp_chord_bridge_probe, cached.data(), &probe);
    ok &= check(calls == 2 && original_args && unwind_ok && probe.rax == reinterpret_cast<uintptr_t>(expected_dst)
        && probe.rsi == projected && probe.before == probe.after && probe.preserved,
        "eligible ABI RSI handoff preserves RAX/nonvolatiles/unwind");
    reg.invalidate_chart_admission(owner.data());
    ok &= check(reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Ready, "temporary unrelated expand");
    reg.invalidate_chart_admission(wrapper.data());
    ok &= check(reg.chart_update_admission(wrapper.data()) != ChartUpdateAdmission::Ready
        && !reg.seal_chart_admission(binding, header), "same wrapper reparse cannot reseal old identity");
    fail_chord_voicing_chart(binding);
    ok &= check(!reg.playback_snapshot() && reg.cleanup_lease().chart_admission.binding == binding
        && reg.chart_update_admission(wrapper.data()) != ChartUpdateAdmission::Stock, "failed retained denies, not stock");
    ok &= check(!reg.publish_playback(selection, updated)
        && !reg.acquire_selection_guard(selection, updated), "failed lease cannot republish");
    ok &= check(reg.retire_cleanup_lease(updated)
        && reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Stock, "real retirement clears identity");

    // Seal-before-publication and exact already-published attach, no sleeping.
    ok &= check(reg.acquire_selection_guard(selection, updated)
        && reg.attach_chart_admission(selection, updated, binding)
        && reg.seal_chart_admission(binding, header)
        && reg.publish_playback_from_selection_guard(selection, updated)
        && reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Ready, "delayed publication transfer");
    reg.revoke_playback(updated); reg.retire_cleanup_lease(updated);
    ok &= check(reg.publish_playback(selection, updated)
        && reg.attach_chart_admission(selection, updated, binding)
        && reg.seal_chart_admission(binding, header), "already published exact lease attach");
    reg.revoke_playback(updated);
    ok &= check(!reg.commit_if_current_chart(binding,
        +[](const ChartAdmissionSnapshot&, void*) noexcept { return true; }, nullptr), "cleanup never projects");
    reg.retire_cleanup_lease(updated);
    auto grouped = std::make_shared<ChordVoicingBinding>(*binding);
    row.group_index = 1;
    auto follower = row; follower.time_str = "01_00";
    grouped->plan = ff7rp::pipeline::derive_chart_event_plan({row, follower});
    if (!check(grouped->plan.valid() && grouped->plan.events.size() == 2, "valid grouped fixture plan")) return 1;
    grouped->event_names = {42,42};
    grouped->event_successors = {1,ff7rp::pipeline::kNoChartEvent};
    put(wrapper, 0x80, NativeChartHeader{reinterpret_cast<uintptr_t>(event.data()),2,2});
    put(event, 0x18, event.data() + 0x90);
    put(event, 0x90, wrapper.data()); put(event, 0x98, grouped->left);
    put(event, 0xa0, event.data()); put(event, 0xb0, uint32_t{3});
    put(event, 0xbc, uint64_t{42}); put(event, 0xd9, uint8_t{1});
    ok &= check(reg.publish_playback(selection, updated)
        && reg.attach_chart_admission(selection, updated, grouped), "grouped admission");
    finish_chord_voicing_expansion(wrapper.data(), true);
    if (!check(reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Ready,
            "grouped seal prerequisite")) return 1;
    const auto child_projection = chord_voicing_bridge_selftest_scope(reg.playback_snapshot(),
        owner.data(), event.data() + 0x90, reinterpret_cast<uintptr_t>(&ff7rp_chord_probe_return),
        reinterpret_cast<uintptr_t>(table.data()), entry, &ff7rp_chord_bridge_probe, cached.data(), &probe);
    ok &= check(reg.chart_update_admission(wrapper.data()) == ChartUpdateAdmission::Ready
        && calls == 3 && probe.rsi == child_projection && original_args && unwind_ok && probe.preserved,
        "charted follower validates root backlinks and projects without extra event");
    if (!ok) return 1;
    ok &= check(chord_voicing_callback_selftest(owner.data(), event.data() + 0x90, true, true),
        "known exact follower calls original once");
    ok &= check(chord_voicing_callback_selftest(owner.data(), event.data() + 1, true, true)
        && chord_voicing_callback_selftest(owner.data(), cached.data(), true, true)
        && reg.playback_snapshot(), "nonmember stock callbacks do not withdraw chart");
    const auto pristine_event = event;
    const auto pristine_controller = controller;
    const auto pristine_wrapper = wrapper;
    for (int fault = 0; fault < 5; ++fault) {
        if (fault == 0) put(event, 0xd9, uint8_t{0});
        if (fault == 1) put(event, 0xbc, uint64_t{999});
        if (fault == 2) put(event, 0xa0, uintptr_t{0});
        if (fault == 3) put(controller, 0xf48, uintptr_t{0});
        if (fault == 4) put(wrapper, 0x88, int32_t{1});
        ok &= check(chord_voicing_callback_selftest(owner.data(), event.data() + 0x90, false, true)
            && reg.playback_snapshot(), "ineligible stock scope still forwards once");
        ok &= check(chord_voicing_callback_selftest(owner.data(), event.data() + 0x90, true, false)
            && !reg.playback_snapshot() && reg.cleanup_lease().chart_admission.binding == grouped
            && !chord_voicing_chart_update_allowed(wrapper.data()),
            "known override invariant failure denies original and retains failed chart");
        if (!ok) return 1;
        event = pristine_event; controller = pristine_controller; wrapper = pristine_wrapper;
        ok &= check(chord_voicing_callback_selftest(owner.data(), event.data(), true, false)
            && chord_voicing_callback_selftest(owner.data(), event.data() + 0x90, true, false),
            "retained root and follower deny subsequent in-flight emissions even with restored fields");
        ok &= check(chord_voicing_callback_selftest(owner.data(), cached.data(), true, true)
            && chord_voicing_callback_selftest(owner.data(), event.data() + 1, true, true)
            && chord_voicing_callback_selftest(owner.data(), event.data(), false, true),
            "retained denial preserves unrelated non-stride and ineligible stock original once");
        ok &= check(!reg.commit_if_current_chart(grouped,
            +[](const ChartAdmissionSnapshot&, void*) noexcept { return true; }, nullptr)
            && !reg.playback_snapshot() && reg.cleanup_lease().chart_admission.binding == grouped,
            "retained denial never restores projection authority");
        if (!ok) return 1;
        reg.retire_cleanup_lease(updated);
        ok &= check(chord_voicing_callback_selftest(owner.data(), event.data(), true, true)
            && chord_voicing_callback_selftest(owner.data(), event.data() + 0x90, true, true),
            "actual retirement removes retained denial identity");
        if (!ok) return 1;
        if (fault != 4) {
            if (!check(reg.publish_playback(selection, updated)
                && reg.attach_chart_admission(selection, updated, grouped), "fault fixture readmission")) return 1;
            finish_chord_voicing_expansion(wrapper.data(), true);
            if (!check(chord_voicing_chart_update_allowed(wrapper.data()), "fault fixture reseal")) return 1;
        }
    }
    std::cout << (ok ? "chord_voicing_selftest: ok\n" : "chord_voicing_selftest: failed\n");
    return ok ? 0 : 1;
}
