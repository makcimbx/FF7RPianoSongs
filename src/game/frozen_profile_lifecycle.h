#pragma once

#include <cstdint>

namespace ff7r::piano::game {

enum class AudioRouteArmStatus {
    Armed,
    Retained,
    Disabled,
    NoRoute,
    Failed,
};

struct AudioRouteLeaseIdentity {
    uint64_t generation = 0;
    uint64_t song_key = 0;

    bool valid() const noexcept { return generation != 0 && song_key != 0; }
    bool operator==(const AudioRouteLeaseIdentity& other) const noexcept
    {
        return generation == other.generation && song_key == other.song_key;
    }
};

struct AudioRouteArmLease {
    AudioRouteArmStatus status = AudioRouteArmStatus::Failed;
    AudioRouteLeaseIdentity identity{};

    bool owns_frozen_profile() const noexcept
    {
        return status == AudioRouteArmStatus::Armed || status == AudioRouteArmStatus::Retained;
    }
};

enum class AudioRouteCleanupEvent {
    RequestException,
    FeatureDisabled,
    RouteValidationFailed,
    NativeClearUnverified,
    VerifiedNoRoute,
    NativeReleaseCommitted,
    CanonicalSubstrateRelinquished,
    ShutdownReleaseVerified,
    ShutdownUnverified,
};

enum class AudioRouteCleanupStatus {
    NoAction,
    VerifiedNoRoute,
    Released,
    Retained,
};

struct AudioRouteCleanupResult {
    AudioRouteCleanupStatus status = AudioRouteCleanupStatus::NoAction;
    AudioRouteLeaseIdentity identity{};
    bool thaw_profile = false;
    bool clear_route_metadata = false;

    bool released() const noexcept
    {
        return status == AudioRouteCleanupStatus::VerifiedNoRoute
            || status == AudioRouteCleanupStatus::Released
            || status == AudioRouteCleanupStatus::NoAction;
    }
};

enum class AudioRouteShutdownStatus {
    Succeeded,
    Retained,
    QuiesceFailed,
};

struct AudioRouteShutdownResult {
    AudioRouteShutdownStatus status = AudioRouteShutdownStatus::Retained;
    AudioRouteCleanupResult route_cleanup{};

    bool succeeded() const noexcept { return status == AudioRouteShutdownStatus::Succeeded; }
};

class FrozenProfileLeaseState {
public:
    void acquire(AudioRouteLeaseIdentity identity) noexcept
    {
        active_ = true;
        identity_ = identity;
        native_arm_attempted_ = false;
        cleanup_metadata_retained_ = true;
    }

    bool active() const noexcept { return active_; }
    AudioRouteLeaseIdentity identity() const noexcept { return identity_; }
    bool native_arm_attempted() const noexcept { return native_arm_attempted_; }
    bool cleanup_metadata_retained() const noexcept { return cleanup_metadata_retained_; }

    bool mark_native_arm_attempt(AudioRouteLeaseIdentity identity) noexcept
    {
        if (!matches(identity)) return false;
        native_arm_attempted_ = true;
        return true;
    }

    AudioRouteCleanupResult transition(
        AudioRouteCleanupEvent event, AudioRouteLeaseIdentity identity) noexcept
    {
        if (!active_) return {};
        if (!matches(identity)) return retained();

        switch (event) {
        case AudioRouteCleanupEvent::VerifiedNoRoute:
            if (native_arm_attempted_) return retained();
            return release(AudioRouteCleanupStatus::VerifiedNoRoute);
        case AudioRouteCleanupEvent::NativeReleaseCommitted:
        case AudioRouteCleanupEvent::CanonicalSubstrateRelinquished:
        case AudioRouteCleanupEvent::ShutdownReleaseVerified:
            return release(AudioRouteCleanupStatus::Released);
        case AudioRouteCleanupEvent::RequestException:
        case AudioRouteCleanupEvent::FeatureDisabled:
        case AudioRouteCleanupEvent::RouteValidationFailed:
        case AudioRouteCleanupEvent::NativeClearUnverified:
        case AudioRouteCleanupEvent::ShutdownUnverified:
            return retained();
        }
        return retained();
    }

private:
    bool matches(AudioRouteLeaseIdentity identity) const noexcept
    {
        return active_ && identity.valid() && identity_ == identity;
    }

    AudioRouteCleanupResult retained() const noexcept
    {
        return {AudioRouteCleanupStatus::Retained, identity_, false, false};
    }

    AudioRouteCleanupResult release(AudioRouteCleanupStatus status) noexcept
    {
        const AudioRouteLeaseIdentity released_identity = identity_;
        reset();
        return {status, released_identity, true, true};
    }

    void reset() noexcept
    {
        active_ = false;
        identity_ = {};
        native_arm_attempted_ = false;
        cleanup_metadata_retained_ = false;
    }

    bool active_ = false;
    AudioRouteLeaseIdentity identity_{};
    bool native_arm_attempted_ = false;
    bool cleanup_metadata_retained_ = false;
};

} // namespace ff7r::piano::game
