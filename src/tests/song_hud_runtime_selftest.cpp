// Exercise the actual orchestration with value-only native/provider doubles.
#include "game/song_hud_runtime.cpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fixture {
using namespace ff7r::piano::game;
std::vector<std::string> logs;
PlaybackSnapshot playback;
bool gate_open=true,array_available=true,root_live=true,font_live=true;
observation::Failure engine_failure=observation::Failure::None;
void* widget=nullptr;
std::size_t bridge_calls=0,measure_calls=0;
std::function<void()> on_draw;
std::function<void(void*)> on_measure;
std::map<void*,float> font_heights;
void* invalid_font=nullptr;
void* drawn_font=nullptr;
float expected_height=16;
bool oversized_title=false,font_zero_serial=false;
float submitted_scale=0;
native::Result bridge_result{native::Outcome::Unavailable,"hud_current_paint"};
void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
bool logged(std::string_view value) {
    return std::any_of(logs.begin(),logs.end(),[&](const auto& line) { return line.find(value)!=std::string::npos; });
}
template<class T> void put(void* p,std::size_t offset,const T& value) {
    std::memcpy(static_cast<std::byte*>(p)+offset,&value,sizeof(value));
}
void __fastcall font_measure(FontMeasure* out,const wchar_t* value) {
    ++measure_calls;
    const auto found=font_heights.find(out->font);
    out->height=found==font_heights.end() ? 16 : found->second;
    out->width=static_cast<float>(std::wcslen(value))*out->height/2;
    if (on_measure) on_measure(out->font);
}
}

namespace ff7r::piano::core {
void log(LogLevel,const std::string& value) { fixture::logs.push_back(value); }
bool bytes_equal(const std::uint8_t*,const std::vector<std::uint8_t>&) { return false; }
bool is_supported_exe(HMODULE,PeIdentity*) { return false; }
bool HookCallbackGate::accepting() const { return fixture::gate_open; }
}
namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() { static core::HookCallbackGate value; return value; }
const RvaSignatureSpec* find_rva_signature(std::string_view) { return nullptr; }
std::uintptr_t song_preview_anchor_root_pair() noexcept { return 0; }
SongRegistry& registry() { static SongRegistry value; return value; }
PlaybackSnapshot SongRegistry::playback_snapshot() const { return fixture::playback; }
RegistrySnapshot SongRegistry::registry_snapshot() const { return {}; }
const SongRegistryStorage& RegistrySnapshot::songs() const noexcept { static SongRegistryStorage empty; return empty; }
bool read_list_guobject_array(runtime_layouts::GUObjectArrayView&) { return fixture::array_available; }
bool read_list_uobject_item_object(const runtime_layouts::GUObjectArrayView&,std::int32_t,void*& out) {
    out=fixture::widget; return true;
}
bool validate_live_uobject_handle(void* p,const UObjectLiveHandle& identity) {
    return fixture::root_live && p==fixture::widget && identity.serial_number>0;
}
bool capture_current_uobject_identity_access(uobject_locator_core::UObjectIdentityAccess&) noexcept { return true; }
}
namespace ff7r::piano::game::uobject_locator_core {
bool capture_live_handle(const UObjectIdentityAccess&,Address p,UObjectLiveHandle& out) noexcept {
    out={1,1}; return !fixture::font_zero_serial && fixture::font_live && p!=reinterpret_cast<Address>(fixture::invalid_font);
}
bool validate_live_handle(const UObjectIdentityAccess&,Address p,const UObjectLiveHandle&) noexcept {
    return fixture::font_live && p!=reinterpret_cast<Address>(fixture::invalid_font);
}
UObjectItemBackedZeroSerialCaptureResult capture_item_backed_zero_serial_snapshot_diagnostic(
    const UObjectIdentityAccess&,Address p,UObjectItemBackedZeroSerialSnapshot& out) noexcept {
    out={1,0};
    return fixture::font_zero_serial && fixture::font_live && p!=reinterpret_cast<Address>(fixture::invalid_font)
        ? UObjectItemBackedZeroSerialCaptureResult::Success : UObjectItemBackedZeroSerialCaptureResult::ResolverOrViewUnavailable;
}
bool validate_item_backed_zero_serial_snapshot(const UObjectIdentityAccess&,Address p,
    const UObjectItemBackedZeroSerialSnapshot&) noexcept {
    return fixture::font_zero_serial && fixture::font_live && p!=reinterpret_cast<Address>(fixture::invalid_font);
}
}
namespace ff7r::piano::game::menu_theme_observation {
Snapshot capture_engine_facts(HMODULE,std::string_view) noexcept {
    Snapshot value; value.failure=fixture::engine_failure; value.failure_site="fixture_engine_facts";
    value.authenticated=value.complete=true; return value;
}
Failure classify_engine_facts(const Snapshot& value) noexcept { return value.failure; }
}
namespace ff7r::piano::game::menu_theme_native {
Result prepare_song_hud_subjects(const CallbackAuthority&) noexcept { return {Outcome::Ready,"hud_names"}; }
Result create(const core::ThemeImage&,const CallbackAuthority&,OwnedImage&) noexcept { return {Outcome::Unavailable,"fixture_no_art"}; }
Result draw_song_hud(const OwnedImage*,void* root,const UObjectLiveHandle& identity,
    std::uintptr_t,float,float,const SongHudMetadata& metadata,const SongHudFontMetrics& metrics,
    void*,void* font,const OverlayDrawText&,const OverlayDrawTile&,const SongHudCanvasAuthority& authority) noexcept {
    ++fixture::bridge_calls;
    fixture::drawn_font=font;
    if (root!=fixture::widget || identity.serial_number<=0 || !authority.current || !authority.current(authority.frame)
        || metadata.bpm!=L"132 BPM" || metrics.line_height!=fixture::expected_height
        || metrics.measure_width(L"AB")!=fixture::expected_height) return {Outcome::Authority,"fixture_bad_frame"};
    if (fixture::on_draw) fixture::on_draw();
    if (fixture::oversized_title) {
        bool allowed=true;
        auto pixels=metrics; pixels.max_scale=1;
        const auto drawn=draw_song_hud_overlay({{300,100,1300,96},1920,1080,1},metadata,pixels,
            nullptr,reinterpret_cast<void*>(1),font,
            [&](void*,float,float,const wchar_t*,void* selected,const runtime_layouts::FLinearColor*,float scale) {
                fixture::submitted_scale=scale;
                allowed=allowed && song_hud_font_draw_scale_allowed(selected,scale); return 0;
            },[](auto&&...) { return true; });
        if (!drawn || !allowed) return {Outcome::Authority,"fixture_font_scale"};
    }
    return fixture::bridge_result;
}
}
int main()
{
    using namespace ff7r::piano::game;
    using namespace fixture;
    try {
        std::array<std::byte,0x1000> controller{},viewport{},engine{},canvas{};
        int root_object=0,font_object=0,fcanvas_object=0,audio_controller=0,control_block=0;
        std::array<std::byte,0x200> chart_object{},wrong_chart{};
        widget=&root_object;
        auto storage=std::make_shared<SongRegistryStorage>(1);
        storage->front().profiles.resize(1); storage->front().profiles.front().bpm=132;
        storage->front().duration_seconds=222;
        PlaybackSnapshot playing;
        playing.storage=storage; playing.song=&storage->front(); playing.profile=&storage->front().profiles.front();
        playing.generation=7; playing.profile_index=0;
        playing.token.registry_generation=7; playing.token.route_generation=3;
        playing.token.lease_generation=4; playing.token.song_key=5; playing.token.controller=&audio_controller;
        auto binding=std::make_shared<ChordVoicingBinding>();
        binding->selection=playing; binding->lease=playing.token;
        binding->controller=controller.data(); binding->wrapper=chart_object.data();
        playing.chart_admission.binding=binding; playing.chart_admission.state=ChartAdmissionState::Ready;
        put(controller.data(),runtime_layouts::PianoChartController::chart,static_cast<void*>(chart_object.data()));
        put(controller.data(),runtime_layouts::PianoChartController::chart_control_block,static_cast<void*>(&control_block));
        put(chart_object.data(),runtime_layouts::PianoScoreWrapper::controller_capture,static_cast<void*>(controller.data()));
        module=reinterpret_cast<HMODULE>(1); measure=font_measure; root_pair=1;
        put(viewport.data(),runtime_layouts::UObject::outer,static_cast<void*>(engine.data()));
        put(engine.data(),runtime_layouts::UEngine::small_font,static_cast<void*>(&font_object));
        put(canvas.data(),runtime_layouts::UCanvas::canvas,static_cast<void*>(&fcanvas_object));
        put(canvas.data(),runtime_layouts::UCanvas::clip_x,1920.0f);
        put(canvas.data(),runtime_layouts::UCanvas::clip_y,1080.0f);
        const auto render=[&] { render_song_hud(viewport.data(),canvas.data(),{},{}); };
        const auto capture=[&] { observe_song_hud_controller(controller.data(),chart_object.data(),playback,9,-1); };
        render(); require(logged("reason=no_current_playback"),"idle is not diagnosed as a missing capture callback");
        playback=playing; render();
        require(logged("reason=no_capture_callback"),"PostRender distinguishes absent chart capture");
        capture();
        require(logged("reason=weak_identity_not_positive"),"zero weak serial remains denied and visible in diagnostics");
        render(); require(logged("reason=no_published_root"),"a failed capture is distinct from no callback");
        const auto before=logs.size();
        for (int i=0;i<1000;++i) render();
        require(logs.size()==before && bridge_calls==0,"silent-frame diagnosis is bounded and cannot draw");
        put(controller.data(),0x6f8,std::array<std::int32_t,2>{2,9});
        const auto rejected=[&](const char* reason) {
            begin_song_hud_tick(); capture();
            require(!state.root.widget,reason); render(); require(bridge_calls==0,reason);
        };
        put(controller.data(),0xf48,static_cast<void*>(wrong_chart.data()));
        rejected("a genuine wrong native chart cannot borrow the current playback binding");
        require(logged("reason=chart_wrapper_mismatch"),"wrong chart has a specific diagnosis");
        put(controller.data(),0xf48,static_cast<void*>(chart_object.data()));
        binding->controller=&audio_controller;
        rejected("the audio controller is not accepted as the binding's piano controller");
        binding->controller=controller.data();
        ++binding->lease.lease_generation;
        rejected("a stale binding lease cannot authorize the current piano controller");
        --binding->lease.lease_generation;
        ++binding->selection.generation;
        rejected("a chart from a stale selection cannot authorize current playback metadata");
        --binding->selection.generation;
        playback.chart_admission.state=ChartAdmissionState::Pending;
        rejected("an unsealed chart does not authorize the HUD");
        playback.chart_admission.state=ChartAdmissionState::Failed;
        rejected("a failed chart does not authorize the HUD");
        playback=playing; playback.token.controller=nullptr;
        begin_song_hud_tick(); capture();
        require(state.root.widget==widget,"publication-only tokens with no audio controller use the proven piano binding");
        playback=playing;
        engine_failure=observation::Failure::GarbageCollecting;
        capture();
        require(logged("stage=capture_engine"),"engine-fact failure is diagnosed without weakening GC guard");
        engine_failure=observation::Failure::None; array_available=false;
        capture();
        require(logged("reason=object_array_unavailable"),"weak resolver read failure is distinct");
        array_available=true;
        begin_song_hud_tick(); capture();
        require(state.root.widget==widget && logged("stage=capture reason=published"),"distinct audio and piano controllers publish through the sealed binding");
        font_live=false; render();
        require(logged("reason=font_identity") && bridge_calls==0,"font admission is reached, diagnosed and fail closed");
        font_live=true;
        begin_song_hud_tick(); capture(); render();
        require(bridge_calls==1 && logged("stage=bridge_omitted reason=hud_current_paint"),"actual orchestration reaches paint bridge");
        bridge_result={native::Outcome::Ready,"hud_submitted"};
        for (int i=0;i<10;++i) begin_song_hud_tick();
        capture(); render();
        require(bridge_calls==2 && measure_calls>0 && logged("stage=bridge_submitted reason=hud_submitted"),"delayed paint readiness submits without waits");
        begin_song_hud_tick(); render(); require(bridge_calls==2,"explicit revocation prevents another render");
        begin_song_hud_tick(); capture();
        ++playback.token.route_generation; render();
        require(logged("reason=root_or_playback_drift") && bridge_calls==2,"changed playback token cannot draw captured root");
        playback=playing;
        begin_song_hud_tick(); capture();
        playback.chart_admission.state=ChartAdmissionState::Failed; render();
        require(bridge_calls==2,"chart invalidation after capture cannot reuse its positive root");
        auto ordinary=playing;
        ordinary.chart_admission={}; // Normal/MIDI publication has no optional voicing binding.
        playback=ordinary;
        begin_song_hud_tick(); capture(); render();
        require(bridge_calls==3,"ordinary playback with a distinct audio controller reaches the bridge without voicings");
        playback.token.controller=nullptr;
        begin_song_hud_tick(); capture(); render();
        require(bridge_calls==4,"ordinary publication-only playback with a null audio controller also draws");
        const auto stale_capture=playback;
        ++playback.token.route_generation;
        begin_song_hud_tick();
        observe_song_hud_controller(controller.data(),chart_object.data(),stale_capture,9,-1);
        require(!state.root.widget,"a stale callback playback snapshot cannot publish a root");
        playback=ordinary;
        put(controller.data(),0xf48,static_cast<void*>(wrong_chart.data()));
        begin_song_hud_tick(); capture(); render();
        require(!state.root.widget && bridge_calls==4,"ordinary playback rejects a chart not owned by this controller");
        put(controller.data(),0xf48,static_cast<void*>(chart_object.data()));
        put(chart_object.data(),runtime_layouts::PianoScoreWrapper::controller_capture,static_cast<void*>(&audio_controller));
        begin_song_hud_tick(); capture(); render();
        require(!state.root.widget && bridge_calls==4,"ordinary playback rejects the wrong reciprocal piano controller");
        put(chart_object.data(),runtime_layouts::PianoScoreWrapper::controller_capture,static_cast<void*>(controller.data()));
        storage->front().chord_voicings.emplace_back();
        begin_song_hud_tick(); capture(); render();
        require(bridge_calls==4,"voicing-enabled playback still requires its sealed optional binding");
        storage->front().chord_voicings.clear();
        put(controller.data(),runtime_layouts::PianoChartController::chart_control_block,static_cast<void*>(nullptr));
        begin_song_hud_tick(); capture(); render();
        require(bridge_calls==4,"missing native control-block ownership remains fail closed");
        put(controller.data(),runtime_layouts::PianoChartController::chart_control_block,static_cast<void*>(&control_block));
        begin_song_hud_tick(); capture();
        require(state.root.widget==widget,"ordinary capture recovers immediately when its real native binding returns");
        ++playback.token.route_generation; render();
        require(bridge_calls==4,"ordinary capture cannot outlive its exact playback token");
        playback=ordinary;
        begin_song_hud_tick(); capture(); begin_song_hud_tick(); render();
        require(bridge_calls==4,"ordinary capture is revoked by an explicit tick/phase retirement");
        begin_song_hud_tick(); capture();
        const auto paused_start=bridge_calls;
        render();
        require(state.root.widget==widget && state.root.submitted,"a successful draw retains only the qualified root");
        for (int i=0;i<3;++i) render();
        require(bridge_calls==paused_start+4 && logged("stage=bridge_retained"),"pause redraws through the fresh bridge without another chart callback");
        begin_song_hud_tick(controller.data()); render();
        require(bridge_calls==paused_start+5,"a same-owner tick without a chart callback does not erase paused presentation");
        capture(); render();
        require(bridge_calls==paused_start+6,"resume requalifies and draws immediately");
        auto attempted=bridge_calls;
        begin_song_hud_tick(&audio_controller); render();
        require(!state.root.widget && bridge_calls==attempted,"a different tick controller retires the old root immediately");
        capture(); begin_song_hud_tick(); render();
        require(bridge_calls==attempted,"explicit exit/phase revocation stops paused draws");
        capture(); playback={}; render();
        require(!state.root.widget && bridge_calls==attempted,"playback retirement stops paused draws without a controller callback");
        playback=ordinary; capture(); ++playback.token.lease_generation; render();
        require(!state.root.widget && bridge_calls==attempted,"an immediate playback switch cannot reuse the retained root");
        playback=ordinary; capture(); root_live=false; render(); root_live=true; render();
        require(!state.root.widget && bridge_calls==attempted,"retired positive identity cannot resurrect without a new capture");
        capture(); engine_failure=observation::Failure::GarbageCollecting; render();
        engine_failure=observation::Failure::None; render();
        require(!state.root.widget && bridge_calls==attempted,"native engine-authority failure retires rather than retaining the paused root");
        capture(); bridge_result={native::Outcome::Unavailable,"hud_current_paint"}; render();
        require(state.root.widget==widget,"temporarily hidden paint retains no geometry, only the current root");
        bridge_result={native::Outcome::Ready,"hud_submitted"}; render();
        require(bridge_calls==attempted+2 && state.root.widget==widget,"fresh paint recovery draws without a chart tick");
        attempted=bridge_calls;
        bridge_result={native::Outcome::Identity,"hud_title_tree"}; render();
        bridge_result={native::Outcome::Ready,"hud_submitted"}; render();
        require(!state.root.widget && bridge_calls==attempted+1,"failed native containment identity retires the retained root");
        capture(); attempted=bridge_calls;
        bridge_result={native::Outcome::Authority,"hud_post_render"}; render();
        bridge_result={native::Outcome::Ready,"hud_submitted"}; render();
        require(!state.root.widget && bridge_calls==attempted+1,"native callback-authority denial cannot retain a paused root");
        capture(); attempted=bridge_calls;
        bridge_result={native::Outcome::Identity,"hud_title_tree"};
        on_draw=[&] { begin_song_hud_tick(controller.data()); }; render(); on_draw={};
        bridge_result={native::Outcome::Ready,"hud_submitted"}; render();
        require(!state.root.widget && bridge_calls==attempted+1,"a same-owner tick during a failed draw cannot rescue the failed capture");
        capture(); attempted=bridge_calls;
        on_draw=[&] { render(); }; render(); on_draw={}; render();
        require(!state.root.widget && bridge_calls==attempted+1,"reentrant PostRender retires the outer frame and cannot retain its root");
        capture(); attempted=bridge_calls;
        on_draw=[] { begin_song_hud_tick(); }; render(); on_draw={}; render();
        require(!state.root.widget && bridge_calls==attempted+1,"revocation during native submission cannot republish the paused root");
        int medium_font=0,large_font=0;
        put(engine.data(),runtime_layouts::UEngine::medium_font,static_cast<void*>(&medium_font));
        put(engine.data(),runtime_layouts::UEngine::large_font,static_cast<void*>(&large_font));
        font_heights[&medium_font]=48; font_heights[&large_font]=32;
        capture(); render();
        require(drawn_font==&font_object && logged("retained_small_font"),
            "retain genuine SmallFont, not another field-selection blur workaround");
        bool scale_checked=false;
        on_draw=[&] {
            scale_checked=song_hud_font_draw_scale_allowed(&font_object,1)
                && !song_hud_font_draw_scale_allowed(&large_font,1)
                && !song_hud_font_draw_scale_allowed(&font_object,NAN);
        };
        render(); on_draw={};
        require(state.root.submitted && scale_checked,"retained native font authority survives synchronous bridge operations");
        require(!song_hud_font_draw_scale_allowed(&font_object,1),"font authority cannot escape its render frame");
        font_zero_serial=true;
        render(); require(drawn_font==&font_object && state.root.submitted,
            "observed genuine zero-serial Runtime font uses item-backed current-field admission");
        oversized_title=true; attempted=bridge_calls;
        render(); render(); oversized_title=false;
        require(submitted_scale==1 && state.root.submitted && bridge_calls==attempted+2,
            "size-keyed pixel metrics remain unscaled and preserve callback-free paused frames");
        invalid_font=&font_object; attempted=bridge_calls;
        render(); require(bridge_calls==attempted && !state.root.widget,"stale retained font cannot reach native text");
        invalid_font=nullptr;
        capture();
        on_measure=[&](void*) { put(engine.data(),runtime_layouts::UEngine::small_font,static_cast<void*>(nullptr)); };
        render(); on_measure={};
        require(bridge_calls==attempted,"font-field drift across native measurement discards the frame");
        put(engine.data(),runtime_layouts::UEngine::small_font,static_cast<void*>(&font_object));
        capture(); render();
        require(state.root.submitted,"delayed font availability recovers without waits");
        on_draw=[&] {
            put(engine.data(),runtime_layouts::UEngine::small_font,static_cast<void*>(nullptr));
            scale_checked=!song_hud_font_draw_scale_allowed(&font_object,1);
        };
        render(); on_draw={};
        require(scale_checked && !state.root.widget,"font-field replacement denies drawing and retires the paused frame");
        font_zero_serial=false;
        DiagnosticBudget bounded;
        for (int i=0;i<100;++i) bounded.take("fixture","reason",i);
        require(bounded.count==bounded.seen.size(),"diagnostic memory and total log count have a hard cap");
        std::cout << "song HUD runtime self-test passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
