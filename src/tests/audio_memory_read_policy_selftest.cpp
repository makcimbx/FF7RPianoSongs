#include "game/audio_memory_read_policy.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "audio_memory_read_policy_selftest: " << message << '\n';
        std::exit(1);
    }
}

void test_ordered_audio_memory_and_guarded_claim_reads()
{
    using namespace ff7r::piano::game;

    void* const controller = reinterpret_cast<void*>(0x1100);
    void* const slot = reinterpret_cast<void*>(0x2200);
    void* const bgm = reinterpret_cast<void*>(0x3300);
    void* const sound = reinterpret_cast<void*>(0x4400);
    void* const seeded_sound = reinterpret_cast<void*>(0xaa00);
    constexpr uint64_t request = 0x5508;
    constexpr uint64_t seeded_request = 0xbb08;
    constexpr uint8_t state = 4;
    constexpr uint8_t seeded_state = 0xcc;

    const auto run_chain = [&](int failure_stage, bool null_result) {
        ControllerAudioChainReadValues values{
            reinterpret_cast<void*>(0xdead), reinterpret_cast<void*>(0xbeef),
            seeded_sound, seeded_request, seeded_state};
        std::vector<int> order;
        const ControllerAudioChainReadFailure failure =
            read_controller_audio_chain_ordered(
                failure_stage == 0 ? nullptr : controller, values,
                [&](void* object, void*& out) {
                    order.push_back(1);
                    if (failure_stage == 1) return false;
                    require(object == controller, "slot read received wrong base");
                    out = failure_stage == 2 && null_result ? nullptr : slot;
                    return true;
                },
                [&](void* object, void*& out) {
                    order.push_back(2);
                    if (failure_stage == 3) return false;
                    require(object == slot, "BGM read received wrong base");
                    out = failure_stage == 4 && null_result ? nullptr : bgm;
                    return true;
                },
                [&](void* object, void*& out) {
                    order.push_back(3);
                    if (failure_stage == 5) return false;
                    require(object == bgm, "sound read received wrong base");
                    out = sound;
                    return true;
                },
                [&](void* object, uint64_t& out) {
                    order.push_back(4);
                    if (failure_stage == 6) return false;
                    require(object == bgm, "request read received wrong base");
                    out = request;
                    return true;
                },
                [&](void* object, uint8_t& out) {
                    order.push_back(5);
                    if (failure_stage == 7) return false;
                    require(object == slot, "state read received wrong base");
                    out = state;
                    return true;
                });
        const ControllerAudioChainReadFailure expected[] = {
            ControllerAudioChainReadFailure::ControllerNull,
            ControllerAudioChainReadFailure::SlotUnreadable,
            ControllerAudioChainReadFailure::SlotNull,
            ControllerAudioChainReadFailure::BgmUnreadable,
            ControllerAudioChainReadFailure::BgmNull,
            ControllerAudioChainReadFailure::SoundUnreadable,
            ControllerAudioChainReadFailure::RequestUnreadable,
            ControllerAudioChainReadFailure::StateUnreadable,
            ControllerAudioChainReadFailure::None,
        };
        require(failure == expected[failure_stage],
            "ordered audio chain returned wrong failure");
        constexpr size_t expected_call_counts[] = {0, 1, 1, 2, 2, 3, 4, 5, 5};
        const size_t expected_calls = expected_call_counts[failure_stage];
        require(order.size() == expected_calls,
            "ordered audio chain did not short-circuit exactly once");
        for (size_t i = 0; i < order.size(); ++i) {
            require(order[i] == static_cast<int>(i + 1),
                "ordered audio chain changed read order");
        }
        require(values.slot == (failure_stage >= 2 && failure_stage != 2 ? slot : nullptr),
            "slot reset/read semantics changed");
        require(values.bgm == (failure_stage >= 4 && failure_stage != 4 ? bgm : nullptr),
            "BGM reset/read semantics changed");
        require(values.sound == (failure_stage >= 6 ? sound : seeded_sound),
            "sound seed/partial-read semantics changed");
        require(values.request_handle == (failure_stage >= 7 ? request : seeded_request),
            "request seed/partial-read semantics changed");
        require(values.state == (failure_stage == 8 ? state : seeded_state),
            "state seed/partial-read semantics changed");
    };
    for (int stage = 0; stage <= 8; ++stage) {
        run_chain(stage, stage == 2 || stage == 4);
    }

    for (int throw_stage = 1; throw_stage <= 5; ++throw_stage) {
        ControllerAudioChainReadValues values{
            nullptr, nullptr, seeded_sound, seeded_request, seeded_state};
        int last_call = 0;
        bool propagated = false;
        try {
            (void)read_controller_audio_chain_ordered(
                controller, values,
                [&](void*, void*& out) {
                    last_call = 1;
                    if (throw_stage == 1) throw std::runtime_error("slot");
                    out = slot;
                    return true;
                },
                [&](void*, void*& out) {
                    last_call = 2;
                    if (throw_stage == 2) throw std::runtime_error("bgm");
                    out = bgm;
                    return true;
                },
                [&](void*, void*& out) {
                    last_call = 3;
                    if (throw_stage == 3) throw std::runtime_error("sound");
                    out = sound;
                    return true;
                },
                [&](void*, uint64_t& out) {
                    last_call = 4;
                    if (throw_stage == 4) throw std::runtime_error("request");
                    out = request;
                    return true;
                },
                [&](void*, uint8_t& out) {
                    last_call = 5;
                    if (throw_stage == 5) throw std::runtime_error("state");
                    out = state;
                    return true;
                });
        } catch (const std::runtime_error&) {
            propagated = true;
        }
        require(propagated && last_call == throw_stage,
            "audio chain swallowed an exception or invoked a later read");
    }

    const auto proof_for = [&](ControllerIdentityProofMode mode) {
        if (mode == ControllerIdentityProofMode::SerialBacked) {
            return make_controller_identity_proof(controller,
                reinterpret_cast<void*>(0x1110), 12, 13,
                reinterpret_cast<void*>(0x1120), true, 17, true, {17, 19});
        }
        if (mode == ControllerIdentityProofMode::ItemBackedZeroSerial) {
            return make_controller_identity_proof(controller,
                reinterpret_cast<void*>(0x1110), 12, 13,
                reinterpret_cast<void*>(0x1120), true, 17, false, {}, true,
                {17, 0});
        }
        return make_controller_identity_proof(controller,
            reinterpret_cast<void*>(0x1110), 12, 13,
            reinterpret_cast<void*>(0x1120), true, -2, false, {});
    };

    enum class GuardFailure { None, Missing, Mismatch, Proof, Chain, Sound, Identity };
    const auto observe = [&](GuardFailure fail, ControllerIdentityProofMode mode,
                             UObjectLiveHandle handle,
                             ControllerAudioChainReadFailure chain_failure =
                                 ControllerAudioChainReadFailure::SoundUnreadable) {
        std::vector<int> order;
        const GuardedPlaySetupClaimReadResult result =
            read_guarded_play_setup_claim_ordered(
                fail == GuardFailure::Missing ? nullptr : controller,
                [&]() {
                    order.push_back(1);
                    return fail == GuardFailure::Mismatch
                        ? reinterpret_cast<void*>(0x1199) : controller;
                },
                [&](void* object, ControllerIdentityProof& out) {
                    order.push_back(2);
                    if (fail == GuardFailure::Proof) return false;
                    require(object == controller, "proof read received wrong controller");
                    out = proof_for(mode);
                    return true;
                },
                [&](void* object, ControllerAudioChainReadValues& out) {
                    order.push_back(3);
                    if (fail == GuardFailure::Chain) {
                        return chain_failure;
                    }
                    require(object == controller, "chain read received wrong controller");
                    out = {slot, bgm,
                        fail == GuardFailure::Sound ? nullptr : sound,
                        request, state};
                    return ControllerAudioChainReadFailure::None;
                },
                [&](void* object, UObjectLiveHandle& out) {
                    order.push_back(4);
                    if (fail == GuardFailure::Identity) return false;
                    require(object == sound, "identity read received wrong sound");
                    out = handle;
                    return true;
                });
        const GuardedPlaySetupClaimReadFailure expected[] = {
            GuardedPlaySetupClaimReadFailure::None,
            GuardedPlaySetupClaimReadFailure::BoundControllerMissing,
            GuardedPlaySetupClaimReadFailure::CurrentControllerMismatch,
            GuardedPlaySetupClaimReadFailure::ControllerProofUnavailable,
            GuardedPlaySetupClaimReadFailure::AudioChainUnreadable,
            GuardedPlaySetupClaimReadFailure::SoundMissing,
            GuardedPlaySetupClaimReadFailure::SoundIdentityUnavailable,
        };
        require(result.failure == expected[static_cast<int>(fail)],
            "guarded claim read returned wrong failure");
        require(result.audio_chain_failure
                == (fail == GuardFailure::Chain
                    ? chain_failure : ControllerAudioChainReadFailure::None),
            "guarded claim read lost or leaked nested chain failure");
        const size_t expected_calls = fail == GuardFailure::Missing ? 0
            : fail == GuardFailure::Mismatch ? 1
            : fail == GuardFailure::Proof ? 2
            : (fail == GuardFailure::Chain || fail == GuardFailure::Sound) ? 3
            : 4;
        require(order.size() == expected_calls,
            "guarded claim read did not short-circuit exactly once");
        for (size_t i = 0; i < order.size(); ++i) {
            require(order[i] == static_cast<int>(i + 1),
                "guarded claim read changed composition order");
        }
        if (fail == GuardFailure::None) {
            require(result.observation.controller == controller
                    && controller_identity_proof_valid(
                        result.observation.controller_proof)
                    && result.observation.slot == slot
                    && result.observation.bgm == bgm
                    && result.observation.sound == sound
                    && result.observation.request_handle == request
                    && result.observation.state == state
                    && result.observation.sound_handle.internal_index
                        == handle.internal_index
                    && result.observation.sound_handle.serial_number
                        == handle.serial_number,
                "guarded claim read did not publish exact full values");
        } else {
            require(result.observation.controller == nullptr
                    && result.observation.slot == nullptr
                    && result.observation.bgm == nullptr
                    && result.observation.sound == nullptr
                    && result.observation.request_handle == 0
                    && result.observation.state == 0xff,
                "failed guarded claim read leaked a partial observation");
        }
        return result;
    };

    const UObjectLiveHandle valid_handle{23, 29};
    for (const ControllerIdentityProofMode mode : {
             ControllerIdentityProofMode::SerialBacked,
             ControllerIdentityProofMode::ItemBackedZeroSerial,
             ControllerIdentityProofMode::Structural}) {
        require(static_cast<bool>(observe(GuardFailure::None, mode, valid_handle)),
            "valid controller proof mode failed guarded composition");
    }
    require(static_cast<bool>(observe(GuardFailure::None,
                ControllerIdentityProofMode::SerialBacked, {})),
        "guarded observation imposed positive-serial sound validity");
    for (const GuardFailure failure : {
             GuardFailure::Missing, GuardFailure::Mismatch, GuardFailure::Proof,
             GuardFailure::Chain, GuardFailure::Sound, GuardFailure::Identity}) {
        require(!static_cast<bool>(observe(failure,
                ControllerIdentityProofMode::SerialBacked, valid_handle)),
            "guarded failure unexpectedly produced an observation");
    }
    for (const ControllerAudioChainReadFailure chain_failure : {
             ControllerAudioChainReadFailure::ControllerNull,
             ControllerAudioChainReadFailure::SlotUnreadable,
             ControllerAudioChainReadFailure::SlotNull,
             ControllerAudioChainReadFailure::BgmUnreadable,
             ControllerAudioChainReadFailure::BgmNull,
             ControllerAudioChainReadFailure::SoundUnreadable,
             ControllerAudioChainReadFailure::RequestUnreadable,
             ControllerAudioChainReadFailure::StateUnreadable}) {
        const GuardedPlaySetupClaimReadResult nested = observe(
            GuardFailure::Chain, ControllerIdentityProofMode::SerialBacked,
            valid_handle, chain_failure);
        require(nested.failure
                    == GuardedPlaySetupClaimReadFailure::AudioChainUnreadable
                && nested.audio_chain_failure == chain_failure
                && nested.observation.controller == nullptr,
            "guarded claim did not preserve exact nested chain failure");
    }

    const GuardedPlaySetupClaimReadResult pre = observe(
        GuardFailure::None, ControllerIdentityProofMode::SerialBacked, valid_handle);
    const GuardedPlaySetupClaimReadResult post = observe(
        GuardFailure::Identity, ControllerIdentityProofMode::SerialBacked, valid_handle);
    require(pre.observation.sound == sound && !post
            && post.observation.sound == nullptr,
        "independent pre/post guarded results retained stale observation state");
    const GuardedPlaySetupClaimReadResult pre_chain = observe(
        GuardFailure::Chain, ControllerIdentityProofMode::SerialBacked,
        valid_handle, ControllerAudioChainReadFailure::RequestUnreadable);
    const GuardedPlaySetupClaimReadResult post_chain = observe(
        GuardFailure::Chain, ControllerIdentityProofMode::SerialBacked,
        valid_handle, ControllerAudioChainReadFailure::StateUnreadable);
    require(pre_chain.audio_chain_failure
                == ControllerAudioChainReadFailure::RequestUnreadable
            && post_chain.audio_chain_failure
                == ControllerAudioChainReadFailure::StateUnreadable
            && pre_chain.observation.controller == nullptr
            && post_chain.observation.controller == nullptr,
        "independent pre/post results reused a nested chain failure");

    for (int throw_stage = 1; throw_stage <= 4; ++throw_stage) {
        int last_call = 0;
        bool propagated = false;
        try {
            (void)read_guarded_play_setup_claim_ordered(
                controller,
                [&]() -> void* {
                    last_call = 1;
                    if (throw_stage == 1) throw std::runtime_error("lookup");
                    return controller;
                },
                [&](void*, ControllerIdentityProof& out) {
                    last_call = 2;
                    if (throw_stage == 2) throw std::runtime_error("proof");
                    out = proof_for(ControllerIdentityProofMode::SerialBacked);
                    return true;
                },
                [&](void*, ControllerAudioChainReadValues& out) {
                    last_call = 3;
                    if (throw_stage == 3) throw std::runtime_error("chain");
                    out = {slot, bgm, sound, request, state};
                    return ControllerAudioChainReadFailure::None;
                },
                [&](void*, UObjectLiveHandle& out) {
                    last_call = 4;
                    if (throw_stage == 4) throw std::runtime_error("identity");
                    out = valid_handle;
                    return true;
                });
        } catch (const std::runtime_error&) {
            propagated = true;
        }
        require(propagated && last_call == throw_stage,
            "guarded composition swallowed an exception or invoked a later callback");
    }
}
} // namespace

int main()
{
    test_ordered_audio_memory_and_guarded_claim_reads();
    return 0;
}
