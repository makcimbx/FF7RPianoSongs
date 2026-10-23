#pragma once

#include <cstddef>
#include <cstdint>

namespace ff7r::piano::game::native_audio {

// Source-equivalent outer traversal for the qualified catalog build/slot profile.
// Access supplies faulting scalar loads and exactly-once captured dispatches;
// this does not validate weak inputs, parse hashes, or retain borrowed receivers.
// Contract/instruction provenance: ChartAdmissionReadOnlyLookupEvidence-20261023.md.
template<class Access>
std::uintptr_t dispatch_native_lookup(Access& access,std::uintptr_t manager,std::uint64_t key,
    std::uint64_t* association,unsigned char* mode,std::uintptr_t gu_chunks,std::uintptr_t gu_count,
    std::size_t backing_slot,std::size_t serialized_interface_slot) {
    using Address=std::uintptr_t;
    const auto resolve=[&](Address entry)->Address {
        const auto serial=access.template load<std::int32_t>(entry+4);
        if(!serial)return 0;
        const auto index=access.template load<std::int32_t>(entry);
        if(index<0 || index>=access.template load<std::int32_t>(gu_count))return 0;
        const auto chunks=access.template load<Address>(gu_chunks);
        const auto chunk=access.template load<Address>(chunks+static_cast<Address>(index>>16)*8);
        const auto item=chunk+static_cast<Address>(index&0xffff)*24;
        if(!item || access.template load<std::int32_t>(item+16)!=serial)return 0;
        if(access.template load<std::uint32_t>(item+8)&0x30000000u)return 0;
        return access.template load<Address>(item);
    };
    *association=0;
    for(unsigned group=0;group<3;++group) {
        const auto header=manager+group*16;
        for(std::int32_t index=0;index<access.template load<std::int32_t>(header+8);++index) {
            const auto entry=access.template load<Address>(header)+static_cast<Address>(index)*16;
            const auto provider=resolve(entry);
            // Native invalid weak entries deliberately fault here, never skip.
            const auto vtable=access.template load<Address>(provider);
            const auto slot=access.template load<unsigned char>(provider+0x54) ? serialized_interface_slot : backing_slot;
            const auto getter=access.template load<Address>(vtable+slot);
            const auto receiver=access.getter(getter,provider);
            const auto ready=access.template load<Address>(access.template load<Address>(receiver)+8);
            if(!access.ready(ready,receiver))continue;
            // Readiness may replace the vtable. Keep receiver, reload target.
            const auto lookup=access.template load<Address>(access.template load<Address>(receiver)+0x28);
            const auto row=access.lookup(lookup,receiver,key);
            if(!row)continue;
            const auto offset=static_cast<Address>(index)*16;
            *association=access.template load<std::uint64_t>(access.template load<Address>(header)+offset+8);
            // Hit-time weak resolution uses a NEW data load after output write.
            const auto hit_provider=resolve(access.template load<Address>(header)+offset);
            *mode=access.template load<unsigned char>(hit_provider+0x54);
            return row;
        }
    }
    return 0; // Native full miss leaves mode unchanged.
}

inline bool native_lookup_stack_outputs(std::uintptr_t association,std::uintptr_t mode,
    std::uintptr_t stack_low,std::uintptr_t stack_high) noexcept {
    return stack_low<stack_high && association%alignof(std::uint64_t)==0
        && association>=stack_low && association<stack_high && stack_high-association>=8
        && mode>=stack_low && mode<stack_high
        && (mode<association || mode-association>=8);
}

} // namespace ff7r::piano::game::native_audio
