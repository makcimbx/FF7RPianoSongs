#pragma once
#include <cstdint>
#include <cstddef>

namespace ff7r::piano::game::native_audio::baseline_facts {
// Memory facts only. Passing this check never creates continuation authority.
using Reader=bool (*)(std::uintptr_t,void*,std::size_t) noexcept;
bool check(std::uintptr_t base,bool post,bool srw,Reader read,const char** failure=nullptr) noexcept;
}
