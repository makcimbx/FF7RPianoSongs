#pragma once
#include "proxy/entry_bridge.h"

namespace ff7rp::proxy {
// Phase-one transport only. No game address or product startup policy is admitted here.
struct CallProfile {
    std::uint32_t call_rva=0, target_rva=0;
    std::uint8_t window_size=0;
    std::array<std::uint8_t,64> window{};
};
enum class CallStage { Empty, Arming, Armed, Consuming, Completed, Rejected, FailedRetained };
enum class CallError { None, Attempt, Root, Profile, Ownership, Allocation, Preparation, Patch, Restore, Unwind, Callback };
struct CallStatus {
    CallStage stage=CallStage::Empty;
    CallError error=CallError::None;
    bool relay_retained=false, callback_invoked=false, callback_succeeded=false;
};
struct CallProof { EntryProof root{}; CallProfile profile{}; std::uintptr_t return_pc=0; };
class CallScope final {
public:
    CallScope(const CallScope&)=delete;
    CallScope& operator=(const CallScope&)=delete;
    bool capture(CallProof&) const noexcept;
private:
    CallScope()=default;
    friend struct CallBridgeAccess;
};
using CallInitializer=bool (*)(const CallScope&,void*) noexcept;
// Caller must retain the owning module and stop continuation on FailedRetained.
// A false callback denies custom startup, not the authentic helper's stock return.
bool arm_owned_call(const EntryScope&,const CallProfile&,CallInitializer,void*,EntryCompletionObserver=nullptr) noexcept;
const CallScope* active_call_scope() noexcept;
CallStatus owned_call_status() noexcept;
} // namespace ff7rp::proxy
