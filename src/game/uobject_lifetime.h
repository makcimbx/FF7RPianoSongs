#pragma once

#include <cstdint>

namespace ff7r::piano::game {

struct UObjectLiveHandle {
    int32_t internal_index = -1;
    int32_t serial_number = 0;
};

bool capture_live_uobject_handle(void* object, UObjectLiveHandle& out);
bool validate_live_uobject_handle(void* object, const UObjectLiveHandle& handle);
bool pin_live_uobject_handle(void* object, const UObjectLiveHandle& handle);
bool is_live_uobject_rooted(void* object, const UObjectLiveHandle& handle);

} // namespace ff7r::piano::game
