#pragma once

#include "game/uobject_lifetime.h"
#include "game/uobject_locator_core.h"

#include <cstdint>
#include <type_traits>

namespace ff7r::piano::game {

constexpr bool private_object_handle_valid(const UObjectLiveHandle& handle) noexcept
{
    return handle.internal_index >= 0 && handle.serial_number > 0;
}

constexpr bool private_object_handle_matches(
    const UObjectLiveHandle& expected, const UObjectLiveHandle& current) noexcept
{
    return private_object_handle_valid(expected)
        && expected.internal_index == current.internal_index
        && expected.serial_number == current.serial_number;
}

enum class ControllerIdentityProofMode {
    Invalid,
    SerialBacked,
    ItemBackedZeroSerial,
    Structural,
};

struct ControllerIdentityProof {
    ControllerIdentityProofMode mode = ControllerIdentityProofMode::Invalid;
    void* controller = nullptr;
    void* object_class = nullptr;
    uint32_t name_comparison_id = 0;
    uint32_t name_number = 0;
    void* outer = nullptr;
    bool raw_internal_index_readable = false;
    int32_t raw_internal_index = -1;
    bool live_capture_succeeded = false;
    UObjectLiveHandle live{};
    bool item_backed_zero_serial_capture_succeeded = false;
    uobject_locator_core::UObjectItemBackedZeroSerialSnapshot
        item_backed_zero_serial{};
};

constexpr bool controller_identity_proof_valid(
    const ControllerIdentityProof& proof) noexcept
{
    if (!proof.controller || !proof.object_class) return false;
    switch (proof.mode) {
    case ControllerIdentityProofMode::SerialBacked:
        return proof.raw_internal_index_readable
            && proof.raw_internal_index >= 0
            && proof.live_capture_succeeded
            && !proof.item_backed_zero_serial_capture_succeeded
            && proof.live.internal_index == proof.raw_internal_index
            && proof.live.serial_number > 0;
    case ControllerIdentityProofMode::ItemBackedZeroSerial:
        return proof.raw_internal_index_readable
            && proof.raw_internal_index >= 0
            && !proof.live_capture_succeeded
            && proof.live.internal_index == -1
            && proof.live.serial_number == 0
            && proof.item_backed_zero_serial_capture_succeeded
            && proof.item_backed_zero_serial.internal_index
                == proof.raw_internal_index
            && proof.item_backed_zero_serial.serial_number == 0;
    case ControllerIdentityProofMode::Structural:
        return proof.raw_internal_index_readable
            && proof.raw_internal_index < 0
            && !proof.live_capture_succeeded
            && !proof.item_backed_zero_serial_capture_succeeded
            && proof.live.internal_index == -1
            && proof.live.serial_number == 0
            && proof.item_backed_zero_serial.internal_index == -1
            && proof.item_backed_zero_serial.serial_number == 0;
    case ControllerIdentityProofMode::Invalid:
        return false;
    }
    return false;
}

enum class ControllerIdentityRawIndexState {
    Unreadable,
    Negative,
    Nonnegative,
};

struct ControllerIdentityProofFacts {
    bool valid = false;
    ControllerIdentityProofMode mode = ControllerIdentityProofMode::Invalid;
    bool raw_internal_index_readable = false;
    ControllerIdentityRawIndexState raw_internal_index_state =
        ControllerIdentityRawIndexState::Unreadable;
    int32_t raw_internal_index = -1;
    bool live_capture_succeeded = false;
    int32_t live_internal_index = -1;
    bool live_internal_index_valid = false;
    bool live_serial_valid = false;
};

constexpr ControllerIdentityProofFacts controller_identity_proof_facts(
    const ControllerIdentityProof& proof) noexcept
{
    ControllerIdentityProofFacts facts;
    facts.valid = controller_identity_proof_valid(proof);
    facts.mode = proof.mode;
    facts.raw_internal_index_readable = proof.raw_internal_index_readable;
    facts.raw_internal_index_state = !proof.raw_internal_index_readable
        ? ControllerIdentityRawIndexState::Unreadable
        : proof.raw_internal_index < 0
            ? ControllerIdentityRawIndexState::Negative
            : ControllerIdentityRawIndexState::Nonnegative;
    facts.raw_internal_index = proof.raw_internal_index;
    facts.live_capture_succeeded = proof.live_capture_succeeded;
    facts.live_internal_index = proof.live.internal_index;
    facts.live_internal_index_valid = proof.live.internal_index >= 0;
    facts.live_serial_valid = proof.live.serial_number > 0;
    return facts;
}

enum class ControllerIdentityProofMismatch {
    None,
    ExpectedInvalid,
    CurrentInvalid,
    Mode,
    ObjectPointer,
    ObjectClass,
    NameComparisonIndex,
    NameNumber,
    Outer,
    RawIndex,
    LiveIndex,
    ItemIndex,
    Serial,
};

struct ControllerIdentityProofMismatchReport {
    ControllerIdentityProofFacts expected;
    ControllerIdentityProofFacts current;
    ControllerIdentityProofMismatch first_mismatch =
        ControllerIdentityProofMismatch::None;
};

constexpr ControllerIdentityProofMismatchReport
classify_controller_identity_proof_mismatch(
    const ControllerIdentityProof& expected,
    const ControllerIdentityProof& current) noexcept
{
    ControllerIdentityProofMismatchReport report{
        controller_identity_proof_facts(expected),
        controller_identity_proof_facts(current),
    };
    if (!report.expected.valid) {
        report.first_mismatch = ControllerIdentityProofMismatch::ExpectedInvalid;
    } else if (!report.current.valid) {
        report.first_mismatch = ControllerIdentityProofMismatch::CurrentInvalid;
    } else if (expected.mode != current.mode) {
        report.first_mismatch = ControllerIdentityProofMismatch::Mode;
    } else if (expected.controller != current.controller) {
        report.first_mismatch = ControllerIdentityProofMismatch::ObjectPointer;
    } else if (expected.object_class != current.object_class) {
        report.first_mismatch = ControllerIdentityProofMismatch::ObjectClass;
    } else if (expected.name_comparison_id != current.name_comparison_id) {
        report.first_mismatch = ControllerIdentityProofMismatch::NameComparisonIndex;
    } else if (expected.name_number != current.name_number) {
        report.first_mismatch = ControllerIdentityProofMismatch::NameNumber;
    } else if (expected.outer != current.outer) {
        report.first_mismatch = ControllerIdentityProofMismatch::Outer;
    } else if (expected.mode == ControllerIdentityProofMode::Structural
        && expected.raw_internal_index != current.raw_internal_index) {
        report.first_mismatch = ControllerIdentityProofMismatch::RawIndex;
    } else if (expected.mode == ControllerIdentityProofMode::SerialBacked
        && expected.live.internal_index != current.live.internal_index) {
        report.first_mismatch = ControllerIdentityProofMismatch::LiveIndex;
    } else if (expected.mode == ControllerIdentityProofMode::SerialBacked
        && expected.live.serial_number != current.live.serial_number) {
        report.first_mismatch = ControllerIdentityProofMismatch::Serial;
    } else if (expected.mode == ControllerIdentityProofMode::ItemBackedZeroSerial
        && expected.item_backed_zero_serial.internal_index
            != current.item_backed_zero_serial.internal_index) {
        report.first_mismatch = ControllerIdentityProofMismatch::ItemIndex;
    } else if (expected.mode == ControllerIdentityProofMode::ItemBackedZeroSerial
        && expected.item_backed_zero_serial.serial_number
            != current.item_backed_zero_serial.serial_number) {
        report.first_mismatch = ControllerIdentityProofMismatch::Serial;
    }
    return report;
}

constexpr ControllerIdentityProof make_controller_identity_proof(
    void* controller, void* object_class,
    uint32_t name_comparison_id, uint32_t name_number,
    void* outer, bool raw_internal_index_readable,
    int32_t raw_internal_index, bool live_capture_succeeded,
    const UObjectLiveHandle& live,
    bool item_backed_zero_serial_capture_succeeded = false,
    const uobject_locator_core::UObjectItemBackedZeroSerialSnapshot&
        item_backed_zero_serial = {}) noexcept
{
    ControllerIdentityProof proof;
    proof.controller = controller;
    proof.object_class = object_class;
    proof.name_comparison_id = name_comparison_id;
    proof.name_number = name_number;
    proof.outer = outer;
    proof.raw_internal_index_readable = raw_internal_index_readable;
    proof.raw_internal_index = raw_internal_index;
    proof.live_capture_succeeded = live_capture_succeeded;
    proof.live = live;
    proof.item_backed_zero_serial_capture_succeeded =
        item_backed_zero_serial_capture_succeeded;
    proof.item_backed_zero_serial = item_backed_zero_serial;
    if (!controller || !object_class) return proof;
    if (raw_internal_index_readable && raw_internal_index >= 0
        && live_capture_succeeded
        && !item_backed_zero_serial_capture_succeeded
        && live.internal_index == raw_internal_index
        && live.serial_number > 0) {
        proof.mode = ControllerIdentityProofMode::SerialBacked;
    } else if (raw_internal_index_readable && raw_internal_index >= 0
        && !live_capture_succeeded
        && live.internal_index == -1 && live.serial_number == 0
        && item_backed_zero_serial_capture_succeeded
        && item_backed_zero_serial.internal_index == raw_internal_index
        && item_backed_zero_serial.serial_number == 0) {
        proof.mode = ControllerIdentityProofMode::ItemBackedZeroSerial;
    } else if (raw_internal_index_readable && raw_internal_index < 0
        && !live_capture_succeeded
        && live.internal_index == -1 && live.serial_number == 0
        && !item_backed_zero_serial_capture_succeeded
        && item_backed_zero_serial.internal_index == -1
        && item_backed_zero_serial.serial_number == 0) {
        proof.mode = ControllerIdentityProofMode::Structural;
    }
    return proof;
}

constexpr bool controller_identity_proof_matches(
    const ControllerIdentityProof& expected,
    const ControllerIdentityProof& current) noexcept
{
    if (!controller_identity_proof_valid(expected)
        || !controller_identity_proof_valid(current)
        || expected.mode != current.mode
        || expected.controller != current.controller
        || expected.object_class != current.object_class
        || expected.name_comparison_id != current.name_comparison_id
        || expected.name_number != current.name_number
        || expected.outer != current.outer) {
        return false;
    }
    return (expected.mode == ControllerIdentityProofMode::Structural
            && expected.raw_internal_index == current.raw_internal_index)
        || (expected.mode == ControllerIdentityProofMode::ItemBackedZeroSerial
            && expected.item_backed_zero_serial.internal_index
                == current.item_backed_zero_serial.internal_index
            && expected.item_backed_zero_serial.serial_number
                == current.item_backed_zero_serial.serial_number)
        || (expected.mode == ControllerIdentityProofMode::SerialBacked
            && private_object_handle_matches(expected.live, current.live));
}

struct GuardedPlaySetupClaimObservation {
    void* controller = nullptr;
    ControllerIdentityProof controller_proof{};
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    UObjectLiveHandle sound_handle{};
    uint64_t request_handle = 0;
    uint8_t state = 0xff;
};

enum class ControllerAudioChainReadFailure : uint8_t {
    None,
    ControllerNull,
    SlotUnreadable,
    SlotNull,
    BgmUnreadable,
    BgmNull,
    SoundUnreadable,
    RequestUnreadable,
    StateUnreadable,
};

struct ControllerAudioChainReadValues {
    void* slot = nullptr;
    void* bgm = nullptr;
    void* sound = nullptr;
    uint64_t request_handle = 0;
    uint8_t state = 0xff;
};

template <typename ReadSlot, typename ReadBgm>
ControllerAudioChainReadFailure read_controller_bgm_chain_ordered(
    void* controller, ControllerAudioChainReadValues& values,
    ReadSlot&& read_slot, ReadBgm&& read_bgm)
{
    values.slot = nullptr;
    values.bgm = nullptr;
    if (!controller) return ControllerAudioChainReadFailure::ControllerNull;
    if (!read_slot(controller, values.slot)) {
        return ControllerAudioChainReadFailure::SlotUnreadable;
    }
    if (!values.slot) return ControllerAudioChainReadFailure::SlotNull;
    if (!read_bgm(values.slot, values.bgm)) {
        return ControllerAudioChainReadFailure::BgmUnreadable;
    }
    if (!values.bgm) return ControllerAudioChainReadFailure::BgmNull;
    return ControllerAudioChainReadFailure::None;
}

template <typename ReadSlot, typename ReadBgm, typename ReadSound,
    typename ReadRequest, typename ReadState>
ControllerAudioChainReadFailure read_controller_audio_chain_ordered(
    void* controller, ControllerAudioChainReadValues& values,
    ReadSlot&& read_slot, ReadBgm&& read_bgm, ReadSound&& read_sound,
    ReadRequest&& read_request, ReadState&& read_state)
{
    const ControllerAudioChainReadFailure prefix =
        read_controller_bgm_chain_ordered(
            controller, values, read_slot, read_bgm);
    if (prefix != ControllerAudioChainReadFailure::None) return prefix;
    if (!read_sound(values.bgm, values.sound)) {
        return ControllerAudioChainReadFailure::SoundUnreadable;
    }
    if (!read_request(values.bgm, values.request_handle)) {
        return ControllerAudioChainReadFailure::RequestUnreadable;
    }
    if (!read_state(values.slot, values.state)) {
        return ControllerAudioChainReadFailure::StateUnreadable;
    }
    return ControllerAudioChainReadFailure::None;
}

enum class GuardedPlaySetupClaimReadFailure : uint8_t {
    None,
    BoundControllerMissing,
    CurrentControllerMismatch,
    ControllerProofUnavailable,
    AudioChainUnreadable,
    SoundMissing,
    SoundIdentityUnavailable,
};

struct GuardedPlaySetupClaimReadResult {
    GuardedPlaySetupClaimReadFailure failure =
        GuardedPlaySetupClaimReadFailure::BoundControllerMissing;
    ControllerAudioChainReadFailure audio_chain_failure =
        ControllerAudioChainReadFailure::None;
    GuardedPlaySetupClaimObservation observation{};

    constexpr explicit operator bool() const noexcept
    {
        return failure == GuardedPlaySetupClaimReadFailure::None;
    }
};

template <typename LookupCurrentController, typename ReadControllerProof,
    typename ReadAudioChain, typename ReadSoundIdentity>
GuardedPlaySetupClaimReadResult read_guarded_play_setup_claim_ordered(
    void* bound_controller, LookupCurrentController&& lookup_current_controller,
    ReadControllerProof&& read_controller_proof,
    ReadAudioChain&& read_audio_chain,
    ReadSoundIdentity&& read_sound_identity)
{
    GuardedPlaySetupClaimReadResult result;
    if (!bound_controller) return result;
    if (lookup_current_controller() != bound_controller) {
        result.failure = GuardedPlaySetupClaimReadFailure::CurrentControllerMismatch;
        return result;
    }

    GuardedPlaySetupClaimObservation candidate;
    candidate.controller = bound_controller;
    if (!read_controller_proof(bound_controller, candidate.controller_proof)) {
        result.failure = GuardedPlaySetupClaimReadFailure::ControllerProofUnavailable;
        return result;
    }

    ControllerAudioChainReadValues chain;
    result.audio_chain_failure = read_audio_chain(bound_controller, chain);
    if (result.audio_chain_failure != ControllerAudioChainReadFailure::None) {
        result.failure = GuardedPlaySetupClaimReadFailure::AudioChainUnreadable;
        return result;
    }
    candidate.slot = chain.slot;
    candidate.bgm = chain.bgm;
    candidate.sound = chain.sound;
    candidate.request_handle = chain.request_handle;
    candidate.state = chain.state;
    if (!candidate.sound) {
        result.failure = GuardedPlaySetupClaimReadFailure::SoundMissing;
        return result;
    }
    if (!read_sound_identity(candidate.sound, candidate.sound_handle)) {
        result.failure = GuardedPlaySetupClaimReadFailure::SoundIdentityUnavailable;
        return result;
    }
    result.failure = GuardedPlaySetupClaimReadFailure::None;
    result.observation = candidate;
    return result;
}

static_assert(std::is_trivially_copyable_v<ControllerAudioChainReadValues>);
static_assert(std::is_trivially_copyable_v<GuardedPlaySetupClaimReadResult>);

} // namespace ff7r::piano::game
