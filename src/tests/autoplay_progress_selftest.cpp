#include <intrin.h>
#include <cstdint>
namespace { void* lookup_caller = nullptr; void* test_return_address() { return lookup_caller; } }
#define _ReturnAddress test_return_address
#include "game/progress.cpp"
#undef _ReturnAddress
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <barrier>
#include <thread>

namespace {
bool assisted = false, assist_during_score = false;
unsigned score_calls = 0;
std::atomic_uint lookup_calls{0};
std::array<std::uint8_t, 16> native_record{};
bool return_null = false;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int __fastcall calculate(void*, void*) { ++score_calls; if (assist_during_score) assisted = true; return 900; }
uintptr_t __fastcall lookup(void*, void*, void*, void*) {
    ++lookup_calls; return return_null ? 0 : reinterpret_cast<uintptr_t>(native_record.data());
}
}
namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() { static core::HookCallbackGate gate; return gate; }
bool autoplay_assisted() noexcept { return assisted; }
void autoplay_observe_native_phase() noexcept {}
}
using namespace ff7r::piano::game;
int main() {
    const auto path = std::filesystem::temp_directory_path()
        / (L"ff7rp-autoplay-progress-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    try {
        g_module_base = 0x10000000; g_module_size = 0x10000000;
        lookup_caller = reinterpret_cast<void*>(g_module_base + rva::ProgressLookupDisplayCaller2);
        g_original_progress_lookup = lookup; g_original_score_calculate = calculate;
        native_record[8] = 42;
        non_audio_hook_gate().open();
        {
            AutoplayResultScope result(false);
            require(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr) == reinterpret_cast<uintptr_t>(native_record.data()),
                "unassisted stock record changed");
        }
        for (bool admitted : {true, false}) {
            if (!admitted) non_audio_hook_gate().close();
            AutoplayResultScope result(true);
            auto* shadow = reinterpret_cast<std::uint8_t*>(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr));
            require(shadow && shadow != native_record.data() && shadow[8] == 42, "assisted stock shadow/gate rejection");
            shadow[0] |= 0x80; shadow[8] = 99;
            require(native_record[0] == 0 && native_record[8] == 42, "stock record mutated");
        }
        return_null = true;
        { AutoplayResultScope result(true); require(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr) == 0,"native null lost"); }
        return_null = false;
        {
            AutoplayResultScope outer(true);
            auto* outer_record = reinterpret_cast<std::uint8_t*>(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr));
            outer_record[8] = 10;
            {
                AutoplayResultScope inner(true);
                auto* inner_record = reinterpret_cast<std::uint8_t*>(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr));
                require(inner_record != outer_record && inner_record[8] == 42, "nested native lookup shadow");
                inner_record[8] = 20;
            }
            require(outer_record[8] == 10 && native_record[8] == 42, "nested lookup shadow lifetime");
        }
        std::barrier rendezvous(2);
        auto concurrent_result = [&](std::uint8_t value) {
            AutoplayResultScope result(true);
            auto* shadow = reinterpret_cast<std::uint8_t*>(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr));
            shadow[8] = value; rendezvous.arrive_and_wait();
            require(shadow[8] == value && native_record[8] == 42, "concurrent native result shadows");
        };
        std::thread first(concurrent_result, std::uint8_t{11}), second(concurrent_result, std::uint8_t{22});
        first.join(); second.join();
        non_audio_hook_gate().open();
        SongDescriptor song; song.id = "AutoplayTest"; song.visible_index = 7;
        SongDifficultyProfile profile; profile.difficulty = 1; song.profiles.push_back(profile);
        registry().replace({song}); registry().set_active_selection(7, 0);
        const auto selection = registry().selection_snapshot();
        const CustomContextToken token{selection.generation, 2, 3, 4};
        require(registry().publish_playback(selection, token), "playback fixture");
        g_scores_ini_path = path.wstring();
        require(save_progress(*selection.song, {true,40,30}, g_scores_ini_path, selection.profile), "seed progress");
        const auto unchanged = [&] {
            const auto record = load_progress(*selection.song,g_scores_ini_path,selection.profile);
            require(record.played && record.high_score == 40 && record.last_score == 30,"assisted custom INI changed");
        };
        assisted = true;
        require(score_calculate_detour(nullptr,nullptr)==900 && score_calculate_detour(nullptr,nullptr)==900
            && score_calls == 2,"both native score phases must forward actual result");
        unchanged(); assisted = false;
        { AutoplayResultScope result(true); require(score_calculate_detour(nullptr,nullptr)==900,"scope taint result"); }
        unchanged(); assist_during_score = true;
        require(score_calculate_detour(nullptr,nullptr)==900,"late taint result"); unchanged();
        assisted = false; assist_during_score = false;
        return_null = true;
        {
            AutoplayResultScope result(true);
            auto* shadow = reinterpret_cast<std::uint8_t*>(progress_lookup_detour(nullptr,nullptr,nullptr,nullptr));
            require(shadow && *reinterpret_cast<int*>(shadow+8)==40,"intentional custom null replacement lost");
            shadow[8] = 99;
        }
        unchanged(); return_null = false;
        require(score_calculate_detour(nullptr,nullptr)==900,"unassisted actual result");
        require(load_progress(*selection.song,g_scores_ini_path,selection.profile).high_score==900,
            "ordinary custom persistence regressed");
        std::filesystem::remove(path);
        std::cout << "autoplay progress selftest passed\n"; return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove(path); std::cerr << e.what() << '\n'; return 1;
    }
}
