#include "game/preview_game_volume.h"
#include "game/generated/rvas.generated.h"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
using namespace ff7r::piano::game;
using Address = std::uintptr_t;
constexpr Address base = 0x140000000, slots = 0x200000000, cat = slots + 0x1000,
    envelopes = slots + 0x2000, name = slots + 0x3000;
struct Fixture {
    std::map<Address, unsigned char> bytes;
    bool teardown = false, drift = false, target_drift = false, slot_drift = false;
    template<class T> void put(Address p, const T& value)
    {
        const auto* data = reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[p + i] = data[i];
    }
    Fixture()
    {
        put(base + rva::AudioCategoryArray, slots);
        put(base + rva::AudioCategoryCount, std::int32_t{2});
        put(base + rva::AudioShutdown, std::int32_t{0});
        put(base + rva::AudioInitialized, std::uint8_t{1});
        put(slots, std::array<Address, 2>{0, cat});
        put(cat + 0x38, envelopes); put(cat + 0x60, std::int32_t{31});
        put(cat + 0x78, name); put(cat + 0x80, std::int32_t{4});
        put(name, std::array<char, 4>{'b','g','m',0});
        put(envelopes + 0x9c, 0.5f);
    }
    static bool copy(void* context, Address p, void* out, std::size_t n) noexcept
    {
        auto& f = *static_cast<Fixture*>(context);
        auto* dest = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = f.bytes.find(p + i);
            if (it == f.bytes.end()) return false;
            dest[i] = it->second;
        }
        if (p == envelopes + 0x9c) {
            if (f.teardown) f.put(base + rva::AudioShutdown, std::int32_t{1});
            if (f.drift) f.put(cat + 0x60, std::int32_t{30});
            if (f.target_drift) f.put(envelopes + 0x9c, 0.25f);
            if (f.slot_drift) f.put(slots, std::array<Address, 2>{cat, 0});
        }
        return true;
    }
    PreviewGameVolume sample(std::string_view profile = FF7RP_TARGET_BUILD_ID,
        bool supported = true, Address image_base = base, std::size_t size = 0x10000000)
    {
        return preview_game_volume_detail::project({this, copy}, image_base, size, profile, supported);
    }
};
void require(bool value) { if (!value) throw std::runtime_error("preview game volume invariant"); }
}
int main()
{
    try {
        if (std::string_view(FF7RP_TARGET_BUILD_ID) == "ff7rebirth-steam-win64-6a16ced2") {
            require(rva::AudioCategoryArray == 0x091d8328 && rva::AudioCategoryCount == 0x091d8320
                && rva::AudioShutdown == 0x091d8378 && rva::AudioInitialized == 0x091d8388);
        } else {
            require(std::string_view(FF7RP_TARGET_BUILD_ID) == "ff7rebirth-steam-win64-68fd6fde"
                && rva::AudioCategoryArray == 0x0939ba80 && rva::AudioCategoryCount == 0x0939ba88
                && rva::AudioShutdown == 0x0939bad8 && rva::AudioInitialized == 0x0939bae8);
        }
        Fixture f;
        require(bool(f.sample()) && f.sample().gain == 0.5f);
        f.put(envelopes + 0x9c, 0.0f); require(bool(f.sample()) && f.sample().gain == 0);
        f.put(envelopes + 0x9c, 1.0f); require(f.sample().gain == 1);
        for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN()}) {
            f.put(envelopes + 0x9c, invalid); require(!f.sample());
        }
        f = {}; require(!f.sample("other") && !f.sample(FF7RP_TARGET_BUILD_ID, false));
        require(!f.sample(FF7RP_TARGET_BUILD_ID, true, 0));
        require(!f.sample(FF7RP_TARGET_BUILD_ID, true, base, rva::AudioCategoryArray));
        require(!f.sample(FF7RP_TARGET_BUILD_ID, true, std::numeric_limits<Address>::max() - 4));
        for (int count : {-1, 257}) {
            f = {}; f.put(base + rva::AudioCategoryCount, count); require(!f.sample());
        }
        f = {}; f.put(base + rva::AudioInitialized, std::uint8_t{0});
        require(f.sample().state == PreviewGameVolumeState::NotReady);
        f.put(base + rva::AudioInitialized, std::uint8_t{1}); require(bool(f.sample()));
        f.put(base + rva::AudioShutdown, std::int32_t{1}); require(!f.sample());
        f = {}; f.teardown = true; require(!f.sample());
        f = {}; f.drift = true; require(!f.sample());
        f = {}; f.target_drift = true; require(!f.sample());
        f = {}; f.slot_drift = true; require(!f.sample());
        f = {}; f.put(base + rva::AudioInitialized, std::uint8_t{2}); require(!f.sample());
        f = {}; f.put(base + rva::AudioCategoryArray, Address{0}); require(!f.sample());
        f = {}; f.put(slots, std::array<Address, 2>{cat, cat}); require(!f.sample());
        for (int count : {0, 3, 33}) {
            f = {}; f.put(cat + 0x80, count); require(!f.sample());
        }
        f = {}; f.put(cat + 0x60, std::int32_t{30}); require(!f.sample());
        f = {}; f.put(name, std::array<char, 4>{'b','g','m','x'}); require(!f.sample());
        f = {}; f.bytes.erase(envelopes + 0x9c); require(!f.sample());
        f = {}; f.put(cat + 0x38, std::numeric_limits<Address>::max() - 1); require(!f.sample());
        std::cout << "preview game volume selftest passed: " << FF7RP_TARGET_BUILD_ID << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
