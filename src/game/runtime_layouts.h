#pragma once

#include "game/ue_types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ff7r::piano::game::runtime_layouts {

struct FString {
    const wchar_t* data = nullptr;
    int32_t num = 0;
    int32_t max = 0;
};

static_assert(sizeof(FString) == 16, "Observed FString view must remain 16 bytes");

struct UObject {
    static constexpr uintptr_t flags = 0x08;
    static constexpr uintptr_t internal_index = 0x0c;
    static constexpr uintptr_t object_class = 0x10;
    static constexpr uintptr_t name = 0x18;
    static constexpr uintptr_t outer = 0x20;
};

struct UCanvas {
    static constexpr uintptr_t clip_x = 0x30;
    static constexpr uintptr_t clip_y = 0x34;
    static constexpr uintptr_t canvas = 0x268;
};

struct UEngine {
    static constexpr uintptr_t small_font = 0x50;
    // Default-build stock font loader/typed reflection; independently gated by the HUD.
    static constexpr uintptr_t medium_font = 0x70;
    static constexpr uintptr_t large_font = 0x90;
};

// HUD optional Runtime-font contract; read only after its exact layout gates pass.
struct UFont {
    static constexpr uintptr_t cache_type = 0x30;
    static constexpr uintptr_t legacy_typeface = 0x140;
};

struct FLinearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

static_assert(sizeof(FLinearColor) == 16);

struct FUObjectItem {
    static constexpr uintptr_t object = 0x00;
    static constexpr uintptr_t flags = 0x08;
    static constexpr uintptr_t serial_number = 0x10;
    static constexpr uintptr_t observed_size = 0x18;
};

static_assert(FUObjectItem::serial_number + sizeof(int32_t) <= FUObjectItem::observed_size);

struct GUObjectArray {
    static constexpr uintptr_t chunks = 0x10;
    static constexpr uintptr_t max_elements = 0x20;
    static constexpr uintptr_t num_elements = 0x24;
    static constexpr uintptr_t max_chunks = 0x28;
    static constexpr uintptr_t num_chunks = 0x2c;
    static constexpr int32_t elements_per_chunk = 65'536;
    static constexpr uintptr_t observed_header_size = 0x30;
};

static_assert(GUObjectArray::num_chunks + sizeof(int32_t) == GUObjectArray::observed_header_size);

struct GUObjectArrayView {
    uint8_t** chunks = nullptr;
    int32_t max_elements = 0;
    int32_t num_elements = 0;
    int32_t max_chunks = 0;
    int32_t num_chunks = 0;
};

struct PianoListEntry {
    FNameValue row_name{};
    uint32_t reserved = 0;
};

static_assert(sizeof(PianoListEntry) == 12,
    "Piano list entries are the observed 12-byte backing array elements");

struct PianoMusicList {
    static constexpr uintptr_t entries = 0x418;
    static constexpr uintptr_t count = 0x420;
    static constexpr uintptr_t capacity = 0x424;
    static constexpr uintptr_t selection_map = 0x428;
    static constexpr uintptr_t saved_selection_name = 0x478;
    static constexpr uintptr_t selected_index = 0x480;
    static constexpr uintptr_t visible_count = 0x570;
    static constexpr int32_t vanilla_count = 5;
    static constexpr int32_t maximum_count = 128;
};

// RebirthBuild1004RvaEvidence-20260808: SelectedEntryGetterActivationReturn.
struct PianoActivationGetterFrame {
    static constexpr std::size_t output_from_return_slot(std::string_view build) noexcept
    {
        // The activation caller uses LEA RDX,[RSP+48h/28h] before CALL pushes the return address.
        if (build == "ff7rebirth-steam-win64-68fd6fde") return 0x48 + 8;
        if (build == "ff7rebirth-steam-win64-6a16ced2") return 0x28 + 8;
        return 0;
    }

    static constexpr bool matches_local_output(
        std::size_t offset, uintptr_t return_slot, uintptr_t output, uintptr_t native_result) noexcept
    {
        return offset && return_slot && output > return_slot
            && output - return_slot == offset && native_result == output;
    }
};

// PianoMenuSessionEvidence-20260729: controller-embedded list, not its widget.
struct PianoListSession {
    static constexpr std::ptrdiff_t controller_list_member = 0x6f0;
    static constexpr uintptr_t widget_weak_handle = 0x7a0;
    static constexpr uintptr_t active_word = 0x7d8;
    static constexpr uint16_t inactive = 0x0000;
    static constexpr uint16_t open = 0x0001;
    static constexpr uint16_t closing = 0x0100;
};

struct PianoScoreWrapper {
    static constexpr uintptr_t event_header = 0x80;
    static constexpr uintptr_t copied_row_count = 0x88;
    static constexpr uintptr_t final_group_index = 0xa3;
    static constexpr uintptr_t controller_capture = 0x118;
};

// Playable513ReserveEvidence: byte offsets in the native event vector element.
struct PianoScoreEvent {
    static constexpr std::size_t size = 0x90;
    static constexpr std::size_t chart = 0x00;
    static constexpr std::size_t side = 0x08;
    static constexpr std::size_t group_root = 0x10;
    static constexpr std::size_t group_chain = 0x18;
    static constexpr std::size_t ordinal = 0x20;
    static constexpr std::size_t time = 0x24;
    static constexpr std::size_t strength = 0x28;
    static constexpr std::size_t name = 0x2c;
    static constexpr std::size_t ignore_sound_data = 0x38;
    static constexpr std::size_t ignore_sound_count = 0x40;
    static constexpr std::size_t ignore_sound_capacity = 0x44;
    static constexpr std::size_t assignment = 0x48;
    static constexpr std::size_t assignment_path = 0x49;
    static constexpr std::size_t result_state = 0x4a;
    static constexpr std::size_t note_type = 0x4b;
    static constexpr std::size_t dot_type = 0x4c;
    static constexpr std::size_t activation_state = 0x4d;
    static constexpr std::size_t callback = 0x50;
    static constexpr std::size_t callback_zero_word = 0x60;
    static constexpr std::size_t callback_vtable = 0x70;
    static constexpr std::size_t callback_chart = 0x78;
    static constexpr std::size_t callback_size = size - callback;
    static constexpr uint8_t unresolved_assignment = 8;
};

struct PianoChartController {
    static constexpr uintptr_t chart = 0xf48;
    static constexpr uintptr_t chart_control_block = 0xf50;
};

struct PianoCompletionOwner {
    static constexpr uintptr_t chart = 0xf48;
    static constexpr uintptr_t duration_seconds = 0x1010;
};

struct SqexSeadController {
    static constexpr uintptr_t slot = 0x28;
};

struct SqexSeadSound {
    static constexpr uintptr_t mabf_source = 0x38;
    static constexpr uintptr_t observed_field398 = 0x398;
    static constexpr uintptr_t observed_field41c = 0x41c;
    static constexpr uintptr_t observed_field420 = 0x420;
    static constexpr uintptr_t observed_field548 = 0x548;
};

struct SqexSeadBgm {
    static constexpr uintptr_t sound = 0x28;
    static constexpr uintptr_t request_handle = 0x48;
    static constexpr uintptr_t requested_mode = 0x54;
    static constexpr uintptr_t mode_key = 0x60;
    static constexpr uintptr_t backing_resource = 0x70;
};

struct SqexSeadSlot {
    static constexpr uintptr_t state = 0x28;
    static constexpr uintptr_t observed_field34 = 0x34;
    static constexpr uintptr_t observed_field48 = 0x48;
    static constexpr uintptr_t observed_field5e = 0x5e;
    static constexpr uintptr_t observed_field9c = 0x9c;
    static constexpr uintptr_t bgm = 0xc0;
};

struct SqexSeadManager {
    static constexpr uintptr_t current_slot = 0x48;
    static constexpr uintptr_t pause_count = 0x54;
};

struct PianoAudioGlobal {
    static constexpr uintptr_t owner = 0x1b0;
};

struct PianoAudioOwner {
    static constexpr uintptr_t state = 0x08;
    static constexpr uintptr_t packed_key = 0x50;
    static constexpr uintptr_t request_index = 0x7c;
};

// ChartAdmissionReadOnlyLookupEvidence-20261023. Provider getter slots are
// build-specific and remain in NativeAudioLayout.
struct DataObjectProvider {
    static constexpr uintptr_t backing_mode = 0x54;
    static constexpr uintptr_t backing = 0x58;
    static constexpr uint8_t serialized_mode = 1;
};

struct DataObjectBacking {
    static constexpr uintptr_t serialized_interface = 0x58;
};

struct DataObjectLookupInterface {
    static constexpr uintptr_t readiness_slot = 0x08;
    static constexpr uintptr_t lookup_slot = 0x28;
};

} // namespace ff7r::piano::game::runtime_layouts
