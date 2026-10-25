#include "game/song_hud_runtime.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "core/version_gate.h"
#include "game/hook_specs.h"
#include "game/chord_voicing_binding.h"
#include "game/menu_theme_observation.h"
#include "game/module_hooks.h"
#include "game/song_preview_anchor.h"
#include "game/uobject_identity.h"

#include <cmath>
#include <algorithm>
#include <cwchar>
#include <limits>
#include <map>
#include <mutex>
#include <array>
#include <string_view>

namespace ff7r::piano::game {
namespace {
namespace native=menu_theme_native;
namespace observation=menu_theme_observation;
struct FontMeasure {
    float x=0,y=0,width=0,height=0,scale_x=1,scale_y=1;
    void* font=nullptr;
    float spacing_x=0,spacing_y=0;
};
static_assert(sizeof(FontMeasure)==0x28 && offsetof(FontMeasure,font)==0x18);
using MeasureFn=void(__fastcall*)(FontMeasure*,const wchar_t*);
HMODULE module=nullptr;
MeasureFn measure=nullptr;
std::uintptr_t root_pair=0;
struct Root {
    PlaybackSnapshot playback;
    void* widget=nullptr;
    UObjectLiveHandle identity{};
    std::uint64_t epoch=0;
    std::uintptr_t controller=0; // Comparison-only tick owner, never dereferenced from PostRender.
    std::uint64_t capture_epoch=0; // Same-owner tick invalidation must not rescue a failed old capture.
    bool submitted=false;
};
// First occurrence of each stage/reason only, with a fixed process-wide cap.
struct DiagnosticBudget {
    struct Key { std::string_view stage,reason; int detail=0; };
    std::array<Key,48> seen{};
    std::size_t count=0;
    bool take(std::string_view stage,std::string_view reason,int detail) noexcept {
        for (std::size_t i=0;i<count;++i)
            if (seen[i].stage==stage && seen[i].reason==reason && seen[i].detail==detail) return false;
        if (count==seen.size()) return false;
        seen[count++]={stage,reason,detail}; return true;
    }
};
struct DiagnosticContext {
    std::uint64_t generation=0;
    int profile=-1,current=-1,pending=-1,weak_index=-1,weak_serial=0;
};
struct State {
    std::mutex mutex;
    Root root;
    std::uint64_t epoch=0;
    std::uint64_t captures=0,published=0,renders=0;
    DiagnosticBudget diagnostics;
    // CPU owners prevent pointer-key reuse; native records already retain carriers.
    struct Cover { native::OwnedImage image; bool prepared=false; }; // Upload dispatched, not GPU completion.
    std::map<std::shared_ptr<const core::ThemeImage>,Cover,
        std::owner_less<std::shared_ptr<const core::ThemeImage>>> covers;
};
auto& state=*new State{};
void diagnostic(const char* stage,const char* reason,const DiagnosticContext& context={},int detail=0) noexcept
{
    try {
        std::uint64_t epoch=0,captures=0,published=0,renders=0;
        {
            std::lock_guard lock(state.mutex);
            if (!state.diagnostics.take(stage,reason,detail)) return;
            epoch=state.epoch; captures=state.captures; published=state.published; renders=state.renders;
        }
        core::log(core::LogLevel::Info,std::string("[song_hud] stage=")+stage+" reason="+reason
            +" detail="+std::to_string(detail)+" playback_generation="+std::to_string(context.generation)
            +" profile="+std::to_string(context.profile)+" current="+std::to_string(context.current)
            +" pending="+std::to_string(context.pending)+" weak_index="+std::to_string(context.weak_index)
            +" weak_serial="+std::to_string(context.weak_serial)+" epoch="+std::to_string(epoch)
            +" capture_callbacks="+std::to_string(captures)+" captures_published="+std::to_string(published)
            +" render_callbacks="+std::to_string(renders));
    } catch (...) {}
}
bool engine_current(observation::Snapshot* captured=nullptr,observation::Failure* classified=nullptr) noexcept
{
    if (!module || !non_audio_hook_gate().accepting()) {
        if (captured) captured->failure_site=module ? "callback_gate_closed" : "not_configured";
        if (classified) *classified=observation::Failure::Incomplete;
        return false;
    }
    const auto facts=observation::capture_engine_facts(module,FF7RP_TARGET_BUILD_ID);
    if (captured) *captured=facts;
    const auto failure=observation::classify_engine_facts(facts);
    if (classified) *classified=failure;
    return failure==observation::Failure::None;
}
const char* engine_reason(observation::Failure failure,const char* site) noexcept
{
    using Failure=observation::Failure;
    switch (failure) {
    case Failure::ThreadUninitialized: return "game_thread_uninitialized";
    case Failure::WrongThread: return "wrong_game_thread";
    case Failure::WrongTlsRole: return "wrong_tls_role";
    case Failure::GarbageCollecting: return "garbage_collecting";
    case Failure::ExitPurge: return "exit_purge";
    case Failure::PackageNotRooted: return "transient_package_not_rooted";
    case Failure::PackageOuter: return "transient_package_outer";
    default: return site;
    }
}
bool same_selection(const SelectionSnapshot& a,const SelectionSnapshot& b) noexcept
{
    return a && b && a.generation==b.generation && a.storage==b.storage
        && a.song==b.song && a.profile==b.profile && a.profile_index==b.profile_index;
}
bool same_playback(const PlaybackSnapshot& a,const PlaybackSnapshot& b) noexcept
{
    return same_selection(a,b) && a.token==b.token
        && a.chart_admission.binding==b.chart_admission.binding
        && a.chart_admission.state==b.chart_admission.state
        && a.chart_admission.header==b.chart_admission.header;
}
bool measured(MeasureFn fn,void* font,const wchar_t* text,FontMeasure& out) noexcept
{
    out={}; out.font=font;
    __try { fn(&out,text); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return std::isfinite(out.width) && out.width>=0 && std::isfinite(out.height) && out.height>0;
}
struct FontCapture {
    void* font=nullptr;
    std::uintptr_t offset=0;
    float height=0;
    uobject_locator_core::UObjectIdentityAccess access{};
    uobject_locator_core::UObjectLiveHandle identity{};
    uobject_locator_core::UObjectItemBackedZeroSerialSnapshot zero{};
    bool positive=false;
};
struct RenderFrame {
    const Root& root;
    DWORD thread=GetCurrentThreadId();
    bool retired=false;
    void* viewport=nullptr;
    void* engine=nullptr;
    FontCapture font{};
};
thread_local RenderFrame* active_render=nullptr;
bool font_current(const RenderFrame& frame,const FontCapture& font) noexcept
{
    namespace objects=uobject_locator_core;
    void* engine=nullptr; void* value=nullptr;
    const auto address=reinterpret_cast<std::uintptr_t>(font.font);
    return font.font && core::safe_read_field(frame.viewport,runtime_layouts::UObject::outer,engine)
        && engine==frame.engine && core::safe_read_field(engine,font.offset,value) && value==font.font
        && (font.positive ? objects::validate_live_handle(font.access,address,font.identity)
            : objects::validate_item_backed_zero_serial_snapshot(font.access,address,font.zero));
}
bool current_render(const void* opaque) noexcept
{
    const auto* frame=active_render;
    if (!frame || frame!=opaque || frame->retired || frame->thread!=GetCurrentThreadId() || !engine_current()) return false;
    {
        std::lock_guard lock(state.mutex);
        if (state.root.epoch!=frame->root.epoch || state.root.widget!=frame->root.widget) return false;
    }
    return (!frame->font.font || font_current(*frame,frame->font))
        && validate_live_uobject_handle(frame->root.widget,frame->root.identity)
        && same_playback(frame->root.playback,registry().playback_snapshot());
}
bool select_font(RenderFrame& frame) noexcept
{
    namespace objects=uobject_locator_core;
    FontCapture candidate{}; candidate.offset=runtime_layouts::UEngine::small_font;
    if (!current_render(&frame)
        || !core::safe_read_field(frame.engine,candidate.offset,candidate.font) || !candidate.font
        || !capture_current_uobject_identity_access(candidate.access)) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(candidate.font);
    candidate.positive=objects::capture_live_handle(candidate.access,address,candidate.identity);
    if (!candidate.positive && objects::capture_item_backed_zero_serial_snapshot_diagnostic(
            candidate.access,address,candidate.zero)!=objects::UObjectItemBackedZeroSerialCaptureResult::Success) return false;
    FontMeasure sample;
    if (!font_current(frame,candidate) || !current_render(&frame)
        || !measured(measure,candidate.font,L"Mg",sample)
        || !font_current(frame,candidate) || !current_render(&frame)) return false;
    candidate.height=sample.height;
    frame.font=candidate;
    return true;
}
bool gate(HMODULE image,const char* id) noexcept
{
    const auto* spec=find_rva_signature(id);
    return image && spec && spec->rva && !spec->expected_prologue.empty()
        && core::bytes_equal(reinterpret_cast<const std::uint8_t*>(image)+spec->rva,spec->expected_prologue);
}
void configured(bool admitted) noexcept
{
    try { core::log(core::LogLevel::Info,admitted
        ? "[song_hud] stage=configure result=admitted visual_alignment=unqualified"
        : "[song_hud] stage=configure result=omitted reason=unproved_build_or_contract"); }
    catch (...) {}
}
}

void configure_song_hud(HMODULE image) noexcept
{
    module=nullptr; measure=nullptr; root_pair=0;
    if (!image || image!=GetModuleHandleW(nullptr) || !core::is_supported_exe(image)
        || !gate(image,"song_hud_root_initialization") || !gate(image,"song_hud_font_measure")
        || !gate(image,"song_hud_widget_opacity") || !gate(image,"song_hud_widget_visibility")) { configured(false); return; }
    root_pair=song_preview_anchor_root_pair();
    if (!root_pair) { configured(false); return; }
    measure=reinterpret_cast<MeasureFn>(reinterpret_cast<std::uintptr_t>(image)
        +find_rva_signature("song_hud_font_measure")->rva);
    module=image;
    configured(true);
}

bool song_hud_font_draw_scale_allowed(void* font,float scale) noexcept
{
    const auto* frame=active_render;
    return font && frame && frame->font.font==font && std::isfinite(scale) && scale>0
        && current_render(frame);
}

void prepare_song_hud_covers(const native::CallbackAuthority& authority) noexcept
{
    if (!module) return;
    try {
        const auto names=native::prepare_song_hud_subjects(authority);
        diagnostic(names ? "prepare_names" : "prepare_names_omitted",names.stage,{},static_cast<int>(names.outcome));
        if (!names) return;
        const auto catalog=registry().registry_snapshot();
        for (const auto& song:catalog.songs()) {
            if (!song.cover) continue;
            {
                std::lock_guard lock(state.mutex);
                if (state.covers.contains(song.cover)) continue;
            }
            native::OwnedImage image;
            const auto result=native::create(*song.cover,authority,image);
            if (result || (result.outcome!=native::Outcome::Busy && result.outcome!=native::Outcome::Authority)) {
                std::lock_guard lock(state.mutex);
                state.covers.emplace(song.cover,State::Cover{image,static_cast<bool>(result)});
            }
            if (!result) {
                core::log(core::LogLevel::Info,std::string("[song_hud] stage=cover_prepare result=omitted reason=")+result.stage);
                if (result.outcome==native::Outcome::Authority || result.outcome==native::Outcome::NativeFault) break;
            }
        }
    } catch (...) {}
}

void begin_song_hud_tick() noexcept
{
    begin_song_hud_tick(nullptr);
}

void begin_song_hud_tick(void* controller) noexcept
{
    std::lock_guard lock(state.mutex);
    if (++state.epoch==0) ++state.epoch;
    if (!controller || state.root.controller!=reinterpret_cast<std::uintptr_t>(controller)) state.root={};
    else state.root.epoch=state.epoch;
}

void observe_song_hud_controller(void* controller,void* callback_chart,
    const PlaybackSnapshot& playback,int current,int pending) noexcept
{
    // A failed new chart admission must not leave the previous successful root usable.
    begin_song_hud_tick();
    { std::lock_guard lock(state.mutex); ++state.captures; }
    DiagnosticContext context{}; context.current=current; context.pending=pending;
    if (!module) { diagnostic("capture","not_configured",context); return; }
    if (current!=9 || (pending!=-1 && pending!=9)) { diagnostic("capture","outside_play",context); return; }
    observation::Snapshot facts{};
    observation::Failure engine_failure{};
    if (!engine_current(&facts,&engine_failure)) {
        diagnostic("capture_engine",engine_reason(engine_failure,facts.failure_site),context,static_cast<int>(engine_failure)); return;
    }
    try {
        context.generation=playback.generation; context.profile=playback.profile_index;
        if (!playback) { diagnostic("capture","no_current_playback",context); return; }
        if (!playback.token.valid()) { diagnostic("capture","invalid_playback_token",context); return; }
        if (!same_playback(playback,registry().playback_snapshot())) { diagnostic("capture","playback_drift",context); return; }
        const auto& chart=playback.chart_admission;
        if (chart.state==ChartAdmissionState::Failed) { diagnostic("capture","chart_not_ready",context); return; }
        if (chart.binding || !playback.song->chord_voicings.empty()) {
            if (!chart.binding || chart.state!=ChartAdmissionState::Ready) { diagnostic("capture","chart_not_ready",context); return; }
            if (!playback.token.same_lease(chart.binding->lease)
                || !same_selection(playback,chart.binding->selection)) { diagnostic("capture","chart_playback_mismatch",context); return; }
            if (chart.binding->controller!=controller) { diagnostic("capture","chart_controller_mismatch",context); return; }
            if (chart.binding->wrapper!=callback_chart) { diagnostic("capture","chart_wrapper_mismatch",context); return; }
        }
        // Ordinary charts have no voicing binding. Borrow the real callback's reciprocal native ownership.
        void* owned_chart=nullptr; void* chart_controller=nullptr; void* control_block=nullptr;
        if (!controller || !callback_chart
            || !core::safe_read_field(controller,runtime_layouts::PianoChartController::chart,owned_chart)
            || !core::safe_read_field(callback_chart,runtime_layouts::PianoScoreWrapper::controller_capture,chart_controller)
            || !core::safe_read_field(controller,runtime_layouts::PianoChartController::chart_control_block,control_block)) {
            diagnostic("capture","chart_binding_read",context); return;
        }
        if (owned_chart!=callback_chart || chart_controller!=controller || !control_block) {
            diagnostic("capture","chart_wrapper_mismatch",context); return;
        }
        struct Weak { std::int32_t index=-1,serial=0; bool operator==(const Weak&) const=default; } weak,again;
        runtime_layouts::GUObjectArrayView objects{};
        void* widget=nullptr;
        if (!controller || !core::safe_read_field(controller,0x6f8,weak)) { diagnostic("capture_root","weak_read",context); return; }
        context.weak_index=weak.index; context.weak_serial=weak.serial;
        if (weak.index<0 || weak.serial<=0) { diagnostic("capture_root","weak_identity_not_positive",context); return; }
        if (!read_list_guobject_array(objects)) { diagnostic("capture_root","object_array_unavailable",context); return; }
        if (!read_list_uobject_item_object(objects,weak.index,widget) || !widget) { diagnostic("capture_root","weak_object_unavailable",context); return; }
        if (!validate_live_uobject_handle(widget,{weak.index,weak.serial})) { diagnostic("capture_root","live_identity",context); return; }
        if (!core::safe_read_field(controller,0x6f8,again) || weak!=again) { diagnostic("capture_root","weak_drift",context); return; }
        if (!same_playback(playback,registry().playback_snapshot())) { diagnostic("capture","playback_drift",context); return; }
        if (!engine_current(&facts,&engine_failure)) {
            diagnostic("capture_engine_recheck",engine_reason(engine_failure,facts.failure_site),context,static_cast<int>(engine_failure)); return;
        }
        {
            std::lock_guard lock(state.mutex);
            state.root={playback,widget,{weak.index,weak.serial},state.epoch,
                reinterpret_cast<std::uintptr_t>(controller),state.epoch}; ++state.published;
        }
        diagnostic("capture","published",context);
    } catch (...) { diagnostic("capture","exception",context); }
}

void render_song_hud(void* viewport,void* canvas,const OverlayDrawText& text,const OverlayDrawTile& tile) noexcept
{
    { std::lock_guard lock(state.mutex); ++state.renders; }
    if (!module || !measure || !root_pair) { diagnostic("render","not_configured"); return; }
    if (!viewport || !canvas) { diagnostic("render","missing_canvas_or_viewport"); return; }
    observation::Snapshot facts{};
    observation::Failure engine_failure{};
    if (!engine_current(&facts,&engine_failure)) {
        begin_song_hud_tick();
        diagnostic("render_engine",engine_reason(engine_failure,facts.failure_site),{},static_cast<int>(engine_failure)); return;
    }
    if (active_render) { active_render->retired=true; begin_song_hud_tick(); diagnostic("render","reentry"); return; }
    DiagnosticContext context{};
    try {
        Root root; native::OwnedImage cover; bool has_cover=false;
        {
            std::lock_guard lock(state.mutex);
            root=state.root;
            if (root.playback && root.playback.song->cover) {
                const auto found=state.covers.find(root.playback.song->cover);
                if (found!=state.covers.end() && found->second.prepared) { cover=found->second.image; has_cover=true; }
            }
        }
        context.generation=root.playback.generation; context.profile=root.playback.profile_index;
        if (!root.widget || !root.playback) {
            bool captured=false;
            { std::lock_guard lock(state.mutex); captured=state.captures!=0; }
            const auto playback=registry().playback_snapshot();
            context.generation=playback.generation; context.profile=playback.profile_index;
            diagnostic("render",!playback ? "no_current_playback"
                : captured ? "no_published_root" : "no_capture_callback",context); return;
        }
        RenderFrame frame{root};
        struct Scope {
            RenderFrame& frame;
            bool retain=false;
            explicit Scope(RenderFrame& f):frame(f) { active_render=&f; }
            ~Scope() {
                const bool keep=retain && !frame.retired;
                frame.retired=true; active_render=nullptr;
                std::lock_guard lock(state.mutex);
                if (!keep && state.root.capture_epoch==frame.root.capture_epoch) state.root={};
            }
        } scope(frame);
        if (!current_render(&frame)) { diagnostic("render","root_or_playback_drift",context); return; }
        void* fcanvas=nullptr;
        float width=0,height=0;
        frame.viewport=viewport;
        if (!core::safe_read_field(viewport,runtime_layouts::UObject::outer,frame.engine) || !frame.engine
            || !core::safe_read_field(canvas,runtime_layouts::UCanvas::canvas,fcanvas) || !fcanvas
            || !core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_x,width)
            || !core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_y,height)) {
            diagnostic("render_font","canvas_or_engine_font_path",context); return;
        }
        if (!select_font(frame)) {
            diagnostic("render_font","font_identity",context); return;
        }
        void* font=frame.font.font;
        diagnostic("render_font","retained_small_font",
            context,static_cast<int>(frame.font.offset));
        SongHudFontMetrics metrics{frame.font.height,[&](std::wstring_view value) {
            FontMeasure size;
            if (!current_render(&frame)) return NAN;
            const std::wstring terminated(value);
            return measured(measure,font,terminated.c_str(),size) && current_render(&frame) ? size.width : NAN;
        }};
        const auto& song=*root.playback.song;
        wchar_t bpm[48]{},duration[48]{};
        const float tempo=root.playback.profile->bpm;
        if (std::isfinite(tempo) && tempo>0 && tempo<=10000)
            std::swprintf(bpm,std::size(bpm),L"%.0f BPM",static_cast<double>(tempo));
        if (std::isfinite(song.duration_seconds) && song.duration_seconds>0 && song.duration_seconds<=86400) {
            const auto seconds=static_cast<unsigned>(std::ceil(song.duration_seconds));
            std::swprintf(duration,std::size(duration),L"%u:%02u",seconds/60,seconds%60);
        }
        const SongHudMetadata metadata{song.album,song.author,bpm,duration};
        // Measure this bounded, single-line row's native height too, including fallback glyphs.
        const auto full=song_hud_metadata(metadata,std::numeric_limits<float>::max(),metrics);
        if (!full.empty()) {
            FontMeasure size;
            if (!current_render(&frame) || !measured(measure,font,full.c_str(),size) || !current_render(&frame)) {
                diagnostic("render_font","metadata_measure_or_identity",context); return;
            }
            metrics.line_height=std::max(metrics.line_height,size.height);
        }
        const auto result=native::draw_song_hud(has_cover ? &cover : nullptr,root.widget,root.identity,
            root_pair,width,height,metadata,metrics,fcanvas,font,text,tile,{&frame,current_render});
        // Hidden/unpainted UI can return without retiring a still-current root; never reuse its geometry.
        scope.retain=(result || result.outcome==native::Outcome::Busy
            || (result.outcome==native::Outcome::Unavailable && std::string_view(result.stage)=="hud_current_paint"))
            && current_render(&frame);
        if (result && scope.retain) {
            std::lock_guard lock(state.mutex);
            if (state.root.epoch==root.epoch) state.root.submitted=true;
        }
        diagnostic(result ? (!scope.retain ? "bridge_retired" : root.submitted ? "bridge_retained" : "bridge_submitted") : "bridge_omitted",
            result.stage,context,static_cast<int>(result.outcome));
    } catch (...) { diagnostic("render","exception",context); }
}
}
