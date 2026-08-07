#pragma once

#include "game/runtime_layouts.h"
#include "game/uobject_locator_core.h"
#include "game/uobject_lifetime.h"

#include <windows.h>

#include <cstdint>

namespace ff7r::piano::game {

using UObjectLiveHandleCaptureResult =
    uobject_locator_core::UObjectLiveHandleCaptureResult;
using UObjectItemBackedZeroSerialSnapshot =
    uobject_locator_core::UObjectItemBackedZeroSerialSnapshot;
using UObjectItemBackedZeroSerialCaptureResult =
    uobject_locator_core::UObjectItemBackedZeroSerialCaptureResult;

bool capture_live_uobject_handle(
    void* object,
    UObjectLiveHandle& out,
    UObjectLiveHandleCaptureResult& result);
const char* uobject_live_handle_capture_result_name(
    UObjectLiveHandleCaptureResult result) noexcept;
bool capture_item_backed_zero_serial_uobject_snapshot(
    void* object,
    UObjectItemBackedZeroSerialSnapshot& out,
    UObjectItemBackedZeroSerialCaptureResult& result);
const char* uobject_item_backed_zero_serial_capture_result_name(
    UObjectItemBackedZeroSerialCaptureResult result) noexcept;

// Runtime composition owns this service. It is initialized before list/chart
// hooks and cleared only after every consumer has torn down.
void initialize_uobject_identity(HMODULE exe_module);
void clear_uobject_identity();

// Chart keeps its legacy raw-pointer contract. These lifecycle calls ensure a
// failed chart restore retains the centralized ChartLazy locator state.
void activate_uobject_identity_chart_consumer();
void release_uobject_identity_chart_consumer();

bool uobject_identity_list_scan_ready();
bool read_list_guobject_array(runtime_layouts::GUObjectArrayView& out);
bool read_list_uobject_item_object(
    const runtime_layouts::GUObjectArrayView& view,
    int32_t index,
    void*& object);

bool read_chart_guobject_array(runtime_layouts::GUObjectArrayView& out);
bool read_chart_uobject_item_object(
    const runtime_layouts::GUObjectArrayView& view,
    int32_t index,
    void*& object);

} // namespace ff7r::piano::game
