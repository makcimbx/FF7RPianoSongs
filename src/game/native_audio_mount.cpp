#include "game/native_audio_mount.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"

#include <cstring>
#include <limits>

namespace ff7r::piano::game::native_audio {
namespace {
// Fixed-size copies, with a complete within-call reread. This is drift detection,
// not a lock or native owner lease. Caller supplies the lifetime fence.
class Reads {
public:
    explicit Reads(MemoryReader memory) : memory_(memory) {}
    template<class T> bool get(Address address, T& value) noexcept {
        static_assert(sizeof(T) <= 80);
        if (count_ == spans_.size() || address < 65536
            || address > std::numeric_limits<Address>::max() - sizeof(T)
            || !memory_.read || !memory_.read(memory_.context, address, &value, sizeof(T))) return false;
        auto& s = spans_[count_++]; s.address = address; s.size = sizeof(T);
        std::memcpy(s.bytes.data(), &value, sizeof(T)); return true;
    }
    bool stable() const noexcept {
        std::array<unsigned char, 80> bytes{};
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& s = spans_[i];
            if (!memory_.read(memory_.context, s.address, bytes.data(), s.size)
                || std::memcmp(bytes.data(), s.bytes.data(), s.size)) return false;
        }
        return true;
    }
private:
    struct Span { Address address; std::size_t size; std::array<unsigned char,80> bytes; };
    MemoryReader memory_;
    std::array<Span, 512> spans_{};
    std::size_t count_ = 0;
};
template<class T, std::size_t N> T at(const std::array<unsigned char,N>& bytes, std::size_t offset) noexcept {
    T result{}; std::memcpy(&result, bytes.data()+offset, sizeof(T)); return result;
}
bool pointer(Address p, Address alignment = 8) noexcept { return p >= 65536 && p % alignment == 0; }
}

bool read_mount_identity(MemoryReader memory, Address base, Address owner, MountIdentity& out) noexcept
{
    out = {};
    if (!native_audio_layout(core::generated::kBuildId) || !rva::PackageStoreOwnerGlobal || !pointer(owner)) return false;
    Reads c{memory}; MountIdentity p{}; p.owner = owner;
    Address published = 0, vtable = 0, second = 0, singleton = 0;
    if (!c.get(base + rva::PackageStoreOwnerGlobal, published) || published != owner + 8
        || !c.get(base + rva::PackageStoreGeneration, p.generation)
        || !c.get(owner, vtable) || vtable != base + rva::PackageStoreOwnerVtable
        || !c.get(owner + 8, second) || second != base + rva::PackageStoreOwnerInterfaceVtable
        || !c.get(owner + 648, p.dispatcher) || !pointer(p.dispatcher)
        || !c.get(base + rva::IoDispatcherGlobal, singleton) || singleton != p.dispatcher
        || !c.get(p.dispatcher + 8, p.implementation) || !pointer(p.implementation)) return false;
    const Address event = p.implementation + 1432;
    std::array<unsigned char,16> header{};
    if (!c.get(event, header)) return false;
    const auto data = at<Address>(header,0);
    const auto count = at<std::int32_t>(header,8), capacity = at<std::int32_t>(header,12);
    if (count < 1 || count > 128 || capacity < count || capacity > 4096 || !pointer(data)) return false;
    unsigned matches = 0;
    for (int i = 0; i < count; ++i) {
        std::array<unsigned char,16> entry{};
        if (!c.get(data + static_cast<Address>(i) * 16, entry)) return false;
        const Address closure = at<Address>(entry,0);
        if (!closure && at<std::int32_t>(entry,8) == 0) continue;
        if (!pointer(closure) || at<std::int32_t>(entry,8) <= 0 || !c.get(closure, vtable)) return false;
        if (vtable != base + rva::PackageStoreSubscriptionVtable) continue;
        std::array<unsigned char,40> closure_bytes{};
        if (!c.get(closure, closure_bytes)) return false;
        if (at<Address>(closure_bytes,24) != owner + 680
            || at<Address>(closure_bytes,32) != base + rva::PackageStoreMountCallback) continue;
        p.subscription = at<std::uint64_t>(closure_bytes,16); p.closure = closure; ++matches;
    }
    if (matches != 1 || !p.subscription || !c.stable()) return false;
    out = p; return true;
}

bool read_package_store(MemoryReader memory, Address store, std::uint64_t key, PackageObservation& out) noexcept
{
    out = {}; if (!native_audio_layout(core::generated::kBuildId) || !pointer(store) || !key) return false;
    Reads c{memory}; const Address map = store + 152;
    std::array<unsigned char,80> header{};
    if (!c.get(map,header)) return false;
    const Address storage = at<Address>(header,0), heap = at<Address>(header,64);
    const auto slots=at<std::int32_t>(header,8), capacity=at<std::int32_t>(header,12);
    const auto free=at<std::int32_t>(header,52), buckets=at<std::int32_t>(header,72);
    if (slots<0 || capacity<slots || capacity>2'000'000 || free<0 || free>slots
        || buckets<1 || buckets>1'048'576 || (buckets & (buckets-1))
        || ((capacity || storage) && !pointer(storage))
        || (heap ? !pointer(heap,4) : buckets != 1)) return false;
    std::int32_t index = -1;
    if (!c.get(heap ? heap + (static_cast<std::uint32_t>(key) & (buckets-1)) * Address{4} : map+56, index)) return false;
    std::array<std::int32_t,64> visited{}; std::size_t count = 0; unsigned matches = 0;
    PackageObservation observation{};
    while (index != -1) {
        if (index<0 || index>=slots || count == visited.size() || count >= static_cast<std::size_t>(slots-free)) return false;
        for (std::size_t i=0; i<count; ++i) if (visited[i] == index) return false;
        visited[count++] = index;
        std::array<unsigned char,24> entry{};
        if (!c.get(storage + static_cast<Address>(index)*24,entry)) return false;
        if (at<std::uint64_t>(entry,0) == key) {
            if (++matches != 1) return false;
            observation.value = at<Address>(entry,8);
            if (!pointer(observation.value) || !c.get(observation.value,observation.metadata)) return false;
        }
        index = at<std::int32_t>(entry,16);
    }
    if (!c.stable()) return false;
    out = observation; return true;
}
} // namespace ff7r::piano::game::native_audio
