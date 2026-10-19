#include "game/song_preview_policy.h"
#include <iostream>
#include <stdexcept>

using namespace ff7r::piano;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("preview policy invariant"); }
}
int main()
{
    try {
        game::SongPreviewPolicy p;
        const game::PreviewIdentity a{1, 10, reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), 5};
        auto b = a; b.selection = 11; b.song = reinterpret_cast<void*>(3); b.row = 6;
        require(p.select(a, 100));
        require(!p.dwell(999) && p.dwell(1000));
        const auto first = p.ticket();
        require(p.complete(first, true)); p.started(true); p.finished();
        require(!p.dwell(99999)); // No automatic replay on the same row.
        require(p.toggle()); // Manual starts even after automatic completion.
        const auto manual = p.ticket();
        require(!p.toggle() && !p.complete(manual, true) && !p.dwell(999999));
        require(p.select(b, 2000) && !p.complete(first, true));
        require(p.dwell(2900)); const auto navigation = p.ticket();
        require(p.select(a, 3000) && !p.complete(navigation, true));
        require(p.dwell(3900)); const auto pending = p.ticket();
        p.cancel(); // Immediate confirmation/close/focus loss before load completes.
        require(!p.complete(pending, true) && !p.dwell(9000));
        require(!p.select(a, 9001)); // Old snapshot cannot restart after cancellation.
        require(p.select(b, 10000) && p.dwell(10900));
        require(p.complete(p.ticket(), true)); p.started(true);
        p.cancel(); // Delayed close during playback uses the same cancellation.
        require(!p.dwell(20000));
        auto profile = b; ++profile.selection;
        require(p.select(profile, 21000) && !p.dwell(99999)); // Same row, no loop.
        p.configure(core::PreviewMode::Manual);
        require(p.select(a, 0) && !p.dwell(100000) && p.toggle());
        require(p.complete(p.ticket(), false) && p.phase() == game::PreviewPhase::Failed);
        require(p.toggle() && p.complete(p.ticket(), true)); p.started(true);
        require(!p.toggle());
        p.configure(core::PreviewMode::Off);
        require(p.select(b, 0) && !p.dwell(99999) && !p.toggle());
        p.configure(core::PreviewMode::Auto);
        require(p.select(a, 0) && p.dwell(900));
        p.cancel(); const auto old = p.ticket();
        require(p.select(b, 1000) && p.dwell(1900) && !p.complete(old, true));
        require(p.complete(p.ticket(), true));

        game::PreviewInputEdge edge;
        require(!edge.update(true, false) && !edge.update(true, true));
        require(!edge.update(false, true) && edge.update(true, true));
        require(!edge.update(true, true));
        require(!edge.update(false, true));
        require(!edge.update(true, false)); // L3+R3 consumes the physical hold.
        require(!edge.update(true, true)); // Releasing L3 must not become R3 input.
        require(!edge.update(false, true) && edge.update(true, true));
        std::cout << "song preview policy selftest passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
