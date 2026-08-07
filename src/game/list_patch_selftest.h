#pragma once
#include "game/native_array_publication.h"
#include "game/uobject_identity.h"
#include "game/song_registry.h"
#include <cstddef>
#include <cstdint>
namespace ff7r::piano::game {
#ifdef FF7RP_LIST_CATALOG_SELFTEST
using ListCatalogIdentityValidator = bool(*)(void*, const UObjectLiveHandle&) noexcept;
using ListCatalogTrace = void(*)(uint8_t) noexcept;
void configure_list_catalog_selftest(ListCatalogIdentityValidator,
    NativeArrayTupleFaultInjection, ListCatalogTrace) noexcept;
std::size_t list_catalog_selftest_owner_count() noexcept;
bool list_catalog_selftest_matches_registry(void*) noexcept;
bool restore_owned_list_patches();
void list_catalog_selftest_lock_owners();
void list_catalog_selftest_unlock_owners();
bool list_profile_initialization_selftest(void*, void*, int32_t,
    SelectionSnapshot&) noexcept;
#endif
}
