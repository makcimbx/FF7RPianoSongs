#include "game/song_preview_anchor.h"

#include "core/logging.h"
#include "core/pe_image.h"
#include "game/hook_specs.h"
#include "game/menu_session_authority.h"
#include "game/menu_ui_refresh.h"
#include "game/runtime_layouts.h"
#include "game/rvas.h"

#include <atomic>

namespace ff7r::piano::game {
namespace {
std::atomic<std::uintptr_t> g_root_pair{0};
bool read_native(std::uintptr_t address,void* out,std::size_t size)
{
    return address && core::safe_copy_bytes(reinterpret_cast<const void*>(address),out,size);
}
} // namespace

void configure_song_preview_anchor(HMODULE module) noexcept
{
    g_root_pair.store(0,std::memory_order_release);
    try {
        PreviewAnchorGate gate;
        constexpr const char* ids[]{"preview_root_shared_getter","preview_root_layout_writer",
            "preview_screen_to_ui","preview_reference_scale","preview_widget_geometry",
            "preview_slate_paint_writer"};
        const auto base=reinterpret_cast<std::uintptr_t>(module);
        for (std::size_t i=0;i<gate.verifiers.size();++i) {
            gate.verifiers[i].name=ids[i];
            if (const auto* spec=find_rva_signature(ids[i]); module && spec && spec->rva
                && spec->rva<=UINTPTR_MAX-base) {
                gate.verifiers[i].address=base+spec->rva;
                gate.verifiers[i].bytes=spec->expected_prologue;
            }
        }
        if (module && rva::PreviewRootPointer && rva::PreviewRootController
            && rva::PreviewRootPointer<=UINTPTR_MAX-base && rva::PreviewRootController<=UINTPTR_MAX-base) {
            gate.root_pointer=base+rva::PreviewRootPointer;
            gate.root_controller=base+rva::PreviewRootController;
        }
        std::string_view failure;
        const bool admitted=validate_preview_anchor_gate(gate,read_native,failure);
        if (admitted) g_root_pair.store(gate.root_pointer,std::memory_order_release);
        core::log(core::LogLevel::Info,
            std::string("[preview] stage=geometry_gate status=")+(admitted ? "admitted" : "unavailable")
                +" reason="+(admitted ? "source_contract_validated" : std::string(failure)));
    } catch (...) {
        g_root_pair.store(0,std::memory_order_release);
    }
}

bool capture_song_preview_row_bounds(const SongPreviewRenderSnapshot& snapshot,
    void*,void* canvas,PreviewSelectedRowBounds& out) noexcept
{
    out={};
    const auto root_pair=g_root_pair.load(std::memory_order_acquire);
    if (!root_pair || !snapshot || !canvas) return false;
    try {
        const auto session=capture_ready_menu_session();
        ListItemUiRefreshTarget item;
        if (!session || session.generation!=snapshot.session_generation
            || !session.widget || !validate_live_uobject_handle(session.widget,session.widget_identity)
            || !capture_active_list_item_ui_target(snapshot.selection,item)) return false;
        float width=0,height=0;
        if (!core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_x,width)
            || !core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_y,height)) return false;
        const auto identity_current=[&] {
            const auto current=capture_song_preview_render_snapshot();
            float current_width=0,current_height=0;
            return current && current.session_generation==snapshot.session_generation
                && current.selection.generation==snapshot.selection.generation && current.ticket==snapshot.ticket
                && menu_session_matches(session.generation,session.widget,session.widget_identity,true)
                && validate_live_uobject_handle(session.widget,session.widget_identity)
                && validate_active_list_item_ui_target(snapshot.selection,item)
                && core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_x,current_width)
                && core::safe_read_field(canvas,runtime_layouts::UCanvas::clip_y,current_height)
                && current_width==width && current_height==height;
        };
        return read_preview_anchor({root_pair,reinterpret_cast<std::uintptr_t>(item.widget),
            reinterpret_cast<std::uintptr_t>(session.widget),8},read_native,identity_current,width,height,out);
    } catch (...) { return false; }
}
} // namespace ff7r::piano::game
