#pragma once

#include "core/config.h"
#include <cstdint>

namespace ff7r::piano::game {

struct PreviewIdentity {
    std::uint64_t session = 0;
    std::uint64_t selection = 0;
    const void* storage = nullptr;
    const void* song = nullptr;
    int row = -1;
    bool operator==(const PreviewIdentity&) const = default;
    bool same_row(const PreviewIdentity& other) const noexcept
    {
        return session == other.session && storage == other.storage
            && song == other.song && row == other.row;
    }
};

enum class PreviewPhase { Idle, Dwell, Loading, Ready, Playing, Finished, Failed };

// Caller serializes this policy with load publication and stream start/stop.
class SongPreviewPolicy {
public:
    void configure(core::PreviewMode mode) noexcept { mode_ = mode; cancel(); }
    bool select(PreviewIdentity identity, std::uint64_t now) noexcept
    {
        if (identity == identity_) return false;
        const bool used = identity.same_row(identity_) && attempted_;
        identity_ = identity;
        ++ticket_;
        attempted_ = used;
        since_ = now;
        phase_ = mode_ == core::PreviewMode::Auto && !used
            ? PreviewPhase::Dwell : PreviewPhase::Idle;
        return true;
    }
    bool dwell(std::uint64_t now) noexcept
    {
        if (mode_ != core::PreviewMode::Auto || phase_ != PreviewPhase::Dwell
            || now - since_ < 900) return false;
        request();
        return true;
    }
    // True requests a load; false means cancellation (or mode Off).
    bool toggle() noexcept
    {
        if (mode_ == core::PreviewMode::Off) return false;
        if (phase_ == PreviewPhase::Loading || phase_ == PreviewPhase::Ready
            || phase_ == PreviewPhase::Playing) { cancel(); return false; }
        request();
        return true;
    }
    bool complete(std::uint64_t ticket, bool success) noexcept
    {
        if (ticket != ticket_ || phase_ != PreviewPhase::Loading) return false;
        phase_ = success ? PreviewPhase::Ready : PreviewPhase::Failed;
        return true;
    }
    void started(bool success) noexcept
    {
        phase_ = success ? PreviewPhase::Playing : PreviewPhase::Failed;
    }
    void finished() noexcept { phase_ = PreviewPhase::Finished; }
    void cancel() noexcept
    {
        ++ticket_;
        attempted_ = true;
        phase_ = PreviewPhase::Finished;
    }
    std::uint64_t ticket() const noexcept { return ticket_; }
    PreviewPhase phase() const noexcept { return phase_; }
    const PreviewIdentity& identity() const noexcept { return identity_; }
private:
    void request() noexcept { ++ticket_; attempted_ = true; phase_ = PreviewPhase::Loading; }
    core::PreviewMode mode_ = core::PreviewMode::Auto;
    PreviewIdentity identity_;
    std::uint64_t ticket_ = 0;
    std::uint64_t since_ = 0;
    bool attempted_ = false;
    PreviewPhase phase_ = PreviewPhase::Idle;
};

// Released-to-held edge; admission changes never turn a held control into input.
class PreviewInputEdge {
public:
    bool update(bool held, bool eligible) noexcept
    {
        const bool edge = held && !held_ && eligible;
        held_ = held;
        return edge;
    }
private:
    bool held_ = false;
};

} // namespace ff7r::piano::game
