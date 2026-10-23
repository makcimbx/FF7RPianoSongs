#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace ff7r::piano::game::native_audio {

enum class NativeEntryKind : unsigned char { Unknown, Lookup, Append, Remove, Reset, NameHelper, Reserve, Count };
inline constexpr const char* native_entry_kind_name(NativeEntryKind kind) noexcept {
    switch(kind) {
    case NativeEntryKind::Lookup: return "lookup";
    case NativeEntryKind::Append: return "append";
    case NativeEntryKind::Remove: return "remove";
    case NativeEntryKind::Reset: return "reset";
    case NativeEntryKind::NameHelper: return "name_helper";
    case NativeEntryKind::Reserve: return "reserve";
    default: return "unknown";
    }
}
enum class NativeEntryGateFailure : unsigned char { None, NestedEntry, ExclusiveTryBusy };
inline constexpr const char* native_entry_gate_failure_name(NativeEntryGateFailure failure) noexcept {
    switch(failure) {
    case NativeEntryGateFailure::None: return "none";
    case NativeEntryGateFailure::NestedEntry: return "current_thread_nested";
    case NativeEntryGateFailure::ExclusiveTryBusy: return "exclusive_try_busy";
    }
    return "unknown";
}
struct NativeEntryGateFailureSnapshot {
    NativeEntryGateFailure reason=NativeEntryGateFailure::None;
    // Non-authoritative, independently sampled counters may race with completion.
    // Zero does not exclude a registration/validation owner or transient contention.
    std::uint32_t observed_entry_mask=0;
};

// The updater never waits for an entrant while holding an engine lock. Native
// registration tries once after the original updater returned; busy means pending.
// Covered foreign entries wait BEFORE native dispatch. Nested entries inherit
// registration admission; opaque/mutating calls strengthen the ROOT admission.
class NativeEntryGate final {
public:
    static bool inside_entry() noexcept { return depth_!=0; }
    static bool owns_registration() noexcept { return registration_owned_; }
    enum class Admission { Opaque, DispatchAware };
    class Reader final {
    public:
        explicit Reader(NativeEntryGate& gate, NativeEntryKind kind=NativeEntryKind::Unknown,
            Admission admission=Admission::Opaque) : gate_(gate),
             kind_(kind<NativeEntryKind::Count ? kind : NativeEntryKind::Unknown) {
            if (!depth_) lock_ = std::shared_lock<std::shared_mutex>(gate_.mutex_);
            if (lock_.owns_lock()) {
                root_=this;
                gate_.entries_[static_cast<unsigned>(kind_)].fetch_add(1,std::memory_order_relaxed);
            }
            ++depth_;
            if(admission==Admission::Opaque) require_mutation();
        }
        ~Reader() {
            --depth_;
            if (lock_.owns_lock()) {
                gate_.entries_[static_cast<unsigned>(kind_)].fetch_sub(1,std::memory_order_relaxed);
                root_=nullptr;
            }
        }
        // Retain nested/opaque admission to the ROOT return, not just the call.
        // Registration bridges inherit actual exclusive ownership, not TLS depth.
        void require_mutation() {
            if(!registration_owned_ && !root_->mutation_.owns_lock())
                root_->mutation_=std::shared_lock<std::shared_mutex>(gate_.mutation_);
        }
        Reader(const Reader&) = delete;
        Reader& operator=(const Reader&) = delete;
    private:
        NativeEntryGate& gate_;
        NativeEntryKind kind_;
        std::shared_lock<std::shared_mutex> lock_;
        std::shared_lock<std::shared_mutex> mutation_;
    };
    class Writer final {
    public:
        explicit Writer(NativeEntryGate& gate) {
            if (!depth_) lock_ = std::unique_lock<std::shared_mutex>(gate.mutex_, std::try_to_lock);
            if (lock_.owns_lock()) {++depth_;registration_owned_=true;}
            else {
                failure_.reason=depth_ ? NativeEntryGateFailure::NestedEntry : NativeEntryGateFailure::ExclusiveTryBusy;
                for(unsigned i=0;i<static_cast<unsigned>(NativeEntryKind::Count);++i)
                    if(gate.entries_[i].load(std::memory_order_relaxed)) failure_.observed_entry_mask|=1u<<i;
            }
        }
        ~Writer() { if (lock_.owns_lock()) {registration_owned_=false;--depth_;} }
        explicit operator bool() const noexcept { return lock_.owns_lock(); }
        NativeEntryGateFailureSnapshot failure() const noexcept { return failure_; }
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;
    private:
        std::unique_lock<std::shared_mutex> lock_;
        NativeEntryGateFailureSnapshot failure_{};
    };
    // Memory-only validation never waits or enters the engine. Pure dispatches
    // share registration admission but not mutation admission.
    class Validation final {
    public:
        explicit Validation(NativeEntryGate& gate) {
            if(!depth_) {
                registration_=std::shared_lock<std::shared_mutex>(gate.mutex_,std::try_to_lock);
                if(registration_.owns_lock()) mutation_=std::unique_lock<std::shared_mutex>(gate.mutation_,std::try_to_lock);
            }
            if(*this) ++depth_;
            else {
                failure_.reason=depth_ ? NativeEntryGateFailure::NestedEntry : NativeEntryGateFailure::ExclusiveTryBusy;
                for(unsigned i=0;i<static_cast<unsigned>(NativeEntryKind::Count);++i)
                    if(gate.entries_[i].load(std::memory_order_relaxed)) failure_.observed_entry_mask|=1u<<i;
            }
        }
        ~Validation(){if(*this)--depth_;}
        explicit operator bool() const noexcept{return mutation_.owns_lock();}
        NativeEntryGateFailureSnapshot failure() const noexcept{return failure_;}
        Validation(const Validation&)=delete;
        Validation& operator=(const Validation&)=delete;
    private:
        std::shared_lock<std::shared_mutex> registration_;
        std::unique_lock<std::shared_mutex> mutation_;
        NativeEntryGateFailureSnapshot failure_{};
    };
private:
    std::shared_mutex mutex_;
    std::shared_mutex mutation_;
    std::array<std::atomic<unsigned>,static_cast<unsigned>(NativeEntryKind::Count)> entries_{};
    // One process-wide manager gate; no other lock domain may share this depth.
    inline static thread_local unsigned depth_ = 0;
    inline static thread_local bool registration_owned_=false;
    inline static thread_local Reader* root_=nullptr;
};

} // namespace ff7r::piano::game::native_audio
