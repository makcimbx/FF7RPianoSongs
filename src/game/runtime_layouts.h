#pragma once

#include "game/ue_types.h"

#include <cstddef>
#include <cstdint>

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
    static constexpr uintptr_t selected_index = 0x480;
    static constexpr uintptr_t visible_count = 0x570;
    static constexpr int32_t vanilla_count = 5;
    static constexpr int32_t maximum_count = 128;
};

struct PianoScoreWrapper {
    static constexpr uintptr_t event_header = 0x80;
    static constexpr uintptr_t copied_row_count = 0x88;
    static constexpr uintptr_t final_group_index = 0xa3;
    static constexpr uintptr_t controller_capture = 0x118;
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

} // namespace ff7r::piano::game::runtime_layouts
