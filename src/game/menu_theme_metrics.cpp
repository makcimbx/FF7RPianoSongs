#include "game/menu_theme_metrics.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <sstream>
#include <windows.h>

namespace ff7r::piano::game::menu_theme_metrics {
namespace {
constexpr std::size_t max_depth = 64;
constexpr std::uint64_t flush_interval_ns = 1000000000;
std::atomic<bool> on{false}, urgent{false};
std::atomic<Sink> sink{nullptr};
std::atomic<std::uint64_t> dropped{0}, last_flush{0};
std::mutex mutex;
std::array<Snapshot, 4> banks{};
std::array<bool, 4> occupied{};
std::uint64_t sequence = 0;
struct Local {
    Stamp* top = nullptr;
    std::size_t depth = 0;
    Context context{};
    Snapshot data{};
};
thread_local Local local;
#ifdef FF7RP_MENU_THEME_METRICS_SELFTEST
std::uint64_t (*fake_clock)() noexcept = nullptr;
#endif
std::uint64_t add(std::uint64_t a, std::uint64_t b) noexcept
{
    return b > (std::numeric_limits<std::uint64_t>::max)() - a
        ? (std::numeric_limits<std::uint64_t>::max)() : a + b;
}
void increment(std::uint64_t& a, std::uint64_t b = 1) noexcept { a = add(a, b); }
std::size_t bucket(std::uint64_t ns) noexcept
{
    std::size_t i = 0;
    while (i < histogram_us.size() && ns > histogram_us[i] * 1000) ++i;
    return i;
}
void record(Stat& s, std::uint64_t ns, std::uint64_t self, bool success) noexcept
{
    increment(s.count); if (!success) increment(s.failed);
    increment(s.inclusive_ns, ns); increment(s.self_ns, self);
    s.max_ns = (std::max)(s.max_ns, ns); increment(s.histogram[bucket(ns)]);
}
Snapshot* bank(std::uint64_t generation) noexcept
{
    for (std::size_t i = 0; i < banks.size(); ++i)
        if (occupied[i] && banks[i].generation == generation) return &banks[i];
    for (std::size_t i = 0; i < banks.size(); ++i) if (!occupied[i]) {
        occupied[i] = true; banks[i] = {}; banks[i].generation = generation; return &banks[i];
    }
    return nullptr;
}
void slow(Snapshot& out, const SlowEvent& event) noexcept
{
    if (event.ns <= out.slow.back().ns) return;
    out.slow.back() = event;
    for (std::size_t i = out.slow.size() - 1; i && out.slow[i].ns > out.slow[i - 1].ns; --i)
        std::swap(out.slow[i], out.slow[i - 1]);
}
void merge(Snapshot& dst, const Snapshot& src) noexcept
{
    increment(dst.roots, src.roots);
    if (!dst.started_ns || (src.started_ns && src.started_ns < dst.started_ns)) dst.started_ns = src.started_ns;
    dst.ended_ns = (std::max)(dst.ended_ns, src.ended_ns);
    for (std::size_t i = 0; i < metric_count; ++i) {
        auto& d = dst.stats[i]; const auto& s = src.stats[i];
        if (!s.count) continue;
        increment(d.count, s.count); increment(d.failed, s.failed);
        increment(d.inclusive_ns, s.inclusive_ns); increment(d.self_ns, s.self_ns);
        d.max_ns = (std::max)(d.max_ns, s.max_ns);
        for (std::size_t j = 0; j < histogram_size; ++j) increment(d.histogram[j], s.histogram[j]);
    }
    for (std::size_t i = 0; i < counter_count; ++i) increment(dst.counters[i], src.counters[i]);
    for (const auto& event : src.slow) if (event.ns) slow(dst, event);
}
template<class F> void update(F&& f) noexcept
{
    if (local.top) { f(local.data); return; }
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock) { dropped.fetch_add(1); return; }
    if (auto* b = bank(0)) f(*b); else dropped.fetch_add(1);
}
} // namespace
void enable(bool value) noexcept { on.store(value); }
bool enabled() noexcept { return on.load(std::memory_order_relaxed); }
void set_sink(Sink value) noexcept { sink.store(value); }
std::uint64_t now_ns() noexcept
{
    if (!enabled()) return 0;
    if (local.top) count(Counter::ClockReads);
#ifdef FF7RP_MENU_THEME_METRICS_SELFTEST
    if (fake_clock) return fake_clock();
#endif
    static const auto frequency = []() noexcept {
        LARGE_INTEGER f{}; return QueryPerformanceFrequency(&f) ? f.QuadPart : 0;
    }();
    LARGE_INTEGER value{};
    if (frequency <= 0 || !QueryPerformanceCounter(&value) || value.QuadPart < 0) {
        count(Counter::ClockFailures); return 0;
    }
    return static_cast<std::uint64_t>(static_cast<long double>(value.QuadPart) * 1000000000.0L / frequency);
}
void begin(Stamp& stamp, Metric metric) noexcept
{
    stamp = {};
    if (!enabled() || static_cast<std::size_t>(metric) >= metric_count) return;
    if (local.depth >= max_depth) { count(Counter::DepthOverflow); return; }
    if (!local.top) { local.data = {}; local.context = {}; }
    stamp.metric = metric; stamp.parent = local.top;
    local.top = &stamp; ++local.depth; stamp.active = true;
    stamp.started = now_ns();
    if (!stamp.parent) local.data.started_ns = stamp.started;
}
void finish(Stamp& stamp, bool success) noexcept
{
    if (!stamp.active) return;
    if (local.top != &stamp) { count(Counter::MalformedScopes); return; }
    const auto end = now_ns();
    const auto ns = end >= stamp.started ? end - stamp.started : 0;
    if (end < stamp.started) count(Counter::ClockFailures);
    record(local.data.stats[static_cast<std::size_t>(stamp.metric)], ns,
        ns >= stamp.children ? ns - stamp.children : 0, success);
    stamp.active = false; local.top = stamp.parent; --local.depth;
    if (stamp.parent) { increment(stamp.parent->children, ns); return; }
    local.data.roots = 1; local.data.ended_ns = end;
    slow(local.data, {stamp.metric, local.context, ns});
    const bool terminal = stamp.metric == Metric::Close
        || local.data.stats[static_cast<std::size_t>(Metric::Close)].count
        || local.data.counters[static_cast<std::size_t>(Counter::ThemeCommits)];
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock) { dropped.fetch_add(1); return; }
    if (auto* b = bank(local.context.generation)) merge(*b, local.data);
    else dropped.fetch_add(1);
    if (terminal) urgent.store(true);
}
void count(Counter counter, std::uint64_t amount) noexcept
{
    if (!enabled() || static_cast<std::size_t>(counter) >= counter_count) return;
    update([&](Snapshot& s) noexcept { increment(s.counters[static_cast<std::size_t>(counter)], amount); });
}
void sample(Metric metric, std::uint64_t ns) noexcept
{
    if (!enabled() || static_cast<std::size_t>(metric) >= metric_count) return;
    update([&](Snapshot& s) noexcept { record(s.stats[static_cast<std::size_t>(metric)], ns, 0, true); });
}
void annotate(const Context& context) noexcept
{
    if (!enabled() || !local.top) return;
    // Keep the outer callback's identity through nested setters/selection reentry.
    if (!local.context.generation) local.context.generation = context.generation;
    if (context.generation && local.context.generation != context.generation) return;
    if (!local.context.setup && local.context.row < 0 && local.context.index < 0) {
        local.context.setup = context.setup; local.context.row = context.row; local.context.index = context.index;
    }
}
bool take_snapshot(Snapshot& out) noexcept
{
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock) return false;
    for (std::size_t i = 0; i < banks.size(); ++i) if (occupied[i]) {
        out = banks[i]; occupied[i] = false; banks[i] = {};
        out.sequence = ++sequence; out.dropped = dropped.exchange(0); return true;
    }
    const auto lost = dropped.exchange(0);
    if (lost) { out = {}; out.sequence = ++sequence; out.dropped = lost; return true; }
    return false;
}
const char* name(Metric value) noexcept
{
    switch (value) {
#define FF7RP_THEME_METRIC_NAME(n) case Metric::n: return #n;
        FF7RP_THEME_METRICS(FF7RP_THEME_METRIC_NAME)
#undef FF7RP_THEME_METRIC_NAME
    default: return "Unknown";
    }
}
const char* name(Counter value) noexcept
{
    switch (value) {
#define FF7RP_THEME_COUNTER_NAME(n) case Counter::n: return #n;
        FF7RP_THEME_COUNTERS(FF7RP_THEME_COUNTER_NAME)
#undef FF7RP_THEME_COUNTER_NAME
    default: return "Unknown";
    }
}
void flush(bool force) noexcept
{
    const auto emit = sink.load();
    if (!enabled() || !emit) return;
    const auto now = now_ns();
    auto previous = last_flush.load();
    if (!force && !urgent.load() && now >= previous && now - previous < flush_interval_ns) return;
    if (!last_flush.compare_exchange_strong(previous, now)) return;
    urgent.store(false);
    try {
        Snapshot snapshot;
        // Fixed drain bound; roots completed during formatting remain for the next window.
        for (std::size_t window = 0; window < banks.size() + 1 && take_snapshot(snapshot); ++window) {
            std::ostringstream batch;
            std::ostringstream out;
            out << "[menu_theme_perf] schema=1 type=window seq=" << snapshot.sequence
                << " generation=" << snapshot.generation << " roots=" << snapshot.roots
                << " dropped=" << snapshot.dropped << " started_ns=" << snapshot.started_ns
                << " ended_ns=" << snapshot.ended_ns;
            batch << out.str() << '\n';
            for (std::size_t i = 0; i < metric_count; ++i) {
                const auto& s = snapshot.stats[i]; if (!s.count) continue;
                std::ostringstream line;
                line << "[menu_theme_perf] schema=1 type=metric seq=" << snapshot.sequence
                    << " generation=" << snapshot.generation << " name=" << name(static_cast<Metric>(i))
                    << " count=" << s.count << " failed=" << s.failed << " inclusive_ns=" << s.inclusive_ns
                    << " self_ns=" << s.self_ns << " max_ns=" << s.max_ns << " hist=";
                for (std::size_t j = 0; j < histogram_size; ++j) { if (j) line << ','; line << s.histogram[j]; }
                batch << line.str() << '\n';
            }
            for (std::size_t i = 0; i < counter_count; ++i) if (snapshot.counters[i]) {
                std::ostringstream line;
                line << "[menu_theme_perf] schema=1 type=counter seq=" << snapshot.sequence
                    << " generation=" << snapshot.generation << " name=" << name(static_cast<Counter>(i))
                    << " value=" << snapshot.counters[i]; batch << line.str() << '\n';
            }
            for (const auto& event : snapshot.slow) if (event.ns) {
                std::ostringstream line;
                line << "[menu_theme_perf] schema=1 type=slow seq=" << snapshot.sequence
                    << " generation=" << snapshot.generation << " name=" << name(event.metric)
                    << " ns=" << event.ns << " row=" << event.context.row << " index=" << event.context.index
                    << " setup=" << event.context.setup; batch << line.str() << '\n';
            }
            auto text = batch.str(); text.pop_back();
            emit(text.c_str()); // One logger acquisition/write per completed window.
        }
    } catch (...) { dropped.fetch_add(1); }
}
#ifdef FF7RP_MENU_THEME_METRICS_SELFTEST
void test_clock(std::uint64_t (*clock)() noexcept) noexcept { fake_clock = clock; }
void test_reset() noexcept
{
    std::lock_guard lock(mutex);
    local = {}; banks = {}; occupied = {}; sequence = 0;
    dropped = 0; urgent = false; last_flush = 0; sink = nullptr;
}
#endif
} // namespace ff7r::piano::game::menu_theme_metrics
