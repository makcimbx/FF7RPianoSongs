#include "game/menu_theme_metrics.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace perf = ff7r::piano::game::menu_theme_metrics;
namespace {
std::atomic<std::uint64_t> clock_ns{0};
std::uint64_t clock_value() noexcept { return clock_ns.load(); }
std::vector<std::string> messages;
void sink(const char* text) noexcept { messages.emplace_back(text); }
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
const perf::Stat& stat(const perf::Snapshot& s, perf::Metric m) { return s.stats[static_cast<std::size_t>(m)]; }
void reset() {
    perf::test_reset(); perf::test_clock(clock_value); perf::enable(true); clock_ns = 0; messages.clear();
}
void qualification_tree(std::uint64_t generation) {
    const auto base = clock_ns.load() + 100;
    clock_ns = base;
    perf::Span root(perf::Metric::QualifyCallback);
    perf::annotate({generation});
    clock_ns = base + 10;
    {
        perf::Span kind(perf::Metric::QualifyPalette);
        clock_ns = base + 20;
        { perf::Span mapping(perf::Metric::QualifyMapping); clock_ns = base + 50; }
        clock_ns = base + 60;
        {
            perf::Span facts(perf::Metric::FactsCapture);
            clock_ns = base + 70;
            { perf::Span session(perf::Metric::FactsSessionSnapshot); clock_ns = base + 80; }
            clock_ns = base + 90;
            { perf::Span identity(perf::Metric::FactsIdentityAccess); clock_ns = base + 100; }
            clock_ns = base + 110;
            {
                perf::Span reads(perf::Metric::FactsDynamicReads);
                clock_ns = base + 120;
                { perf::Span object(perf::Metric::FactsObjectSnapshot); clock_ns = base + 130; }
                clock_ns = base + 140;
            }
            clock_ns = base + 150;
        }
        clock_ns = base + 160;
        { perf::Span classify(perf::Metric::FactsClassify); clock_ns = base + 170; }
        clock_ns = base + 180;
    }
    clock_ns = base + 200;
}
}
int main(int argc, char** argv)
{
    perf::Snapshot s;
    perf::enable(false);
    { perf::Span off(perf::Metric::Apply); perf::count(perf::Counter::ThemeCommits); }
    require(!perf::take_snapshot(s), "disabled capture must do nothing");
    reset(); clock_ns = 1000;
    {
        perf::Span root(perf::Metric::Ready);
        perf::annotate({3, 11, 7, 4});
        clock_ns = 10000;
        {
            perf::Span child(perf::Metric::NativeColorSetter);
            perf::annotate({9, 22, 8, 5}); clock_ns = 12000;
        }
        perf::sample(perf::Metric::RowPendingDelay, 500000);
        perf::count(perf::Counter::SourceRecaptures, 2); clock_ns = 20000;
    }
    require(perf::take_snapshot(s) && s.generation == 3 && s.roots == 1, "outer generation preserved");
    require(stat(s, perf::Metric::Ready).inclusive_ns == 19000
        && stat(s, perf::Metric::Ready).self_ns == 17000, "inclusive/self nesting must not double count");
    require(stat(s, perf::Metric::NativeColorSetter).inclusive_ns == 2000, "real helper duration");
    require(stat(s, perf::Metric::RowPendingDelay).self_ns == 0, "latency samples are not CPU work");
    require(s.slow[0].context.setup == 11 && s.slow[0].context.row == 7, "outer identity preserved");
    require(s.counters[static_cast<std::size_t>(perf::Counter::SourceRecaptures)] == 2, "factual counters");
    for (const auto& item : s.stats) {
        std::uint64_t total = 0; for (auto n : item.histogram) total += n;
        require(total == item.count, "histogram accounts for every measurement");
    }
    require(!perf::take_snapshot(s), "drained snapshot is not emitted twice");
    reset(); qualification_tree(3);
    require(perf::take_snapshot(s) && s.roots == 1, "qualification stages share one root");
    require(stat(s, perf::Metric::QualifyCallback).inclusive_ns == 200
        && stat(s, perf::Metric::QualifyCallback).self_ns == 30
        && stat(s, perf::Metric::QualifyMapping).inclusive_ns == 30
        && stat(s, perf::Metric::FactsCapture).inclusive_ns == 90
        && stat(s, perf::Metric::FactsObjectSnapshot).count == 1,
        "qualification mapping/facts stages remain separately attributable");
    std::uint64_t partitioned_self = 0, native_self = 0;
    for (std::size_t i = 0; i < perf::metric_count; ++i) {
        partitioned_self += s.stats[i].self_ns;
        if (std::string(perf::name(static_cast<perf::Metric>(i))).rfind("Native", 0) == 0)
            native_self += s.stats[i].self_ns;
    }
    require(partitioned_self == 200 && native_self == 0,
        "qualification stages partition time without becoming native helper costs");
    reset();
    {
        perf::Span root(perf::Metric::SelectionHook); perf::annotate({3, 0, -1, 4});
        perf::annotate({3, 22, 8, 5}); clock_ns = 100;
    }
    require(perf::take_snapshot(s) && s.slow[0].context.index == 4
        && s.slow[0].context.setup == 0 && s.slow[0].context.row == -1,
        "semantic-only outer selection remains factual through nested row callbacks");
    reset();
    {
        perf::Stamp root{}, call{};
        perf::begin(root, perf::Metric::Apply);
        clock_ns = 100; perf::begin(call, perf::Metric::NativeBrushAssign);
        clock_ns = 400; perf::finish(call, false);
        clock_ns = 600; perf::finish(root);
    }
    require(perf::take_snapshot(s) && stat(s, perf::Metric::NativeBrushAssign).failed == 1
        && stat(s, perf::Metric::Apply).self_ns == 300, "trivial SEH-compatible stamps preserve failure/self time");
    reset();
    {
        std::array<perf::Stamp, 66> scopes{};
        for (auto& scope : scopes) perf::begin(scope, perf::Metric::Apply);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) perf::finish(*it);
    }
    require(perf::take_snapshot(s) && s.counters[static_cast<std::size_t>(perf::Counter::DepthOverflow)] == 2,
        "bounded nesting reports dropped detail without changing behavior");
    reset();
    {
        perf::Span root(perf::Metric::Close);
        perf::count(perf::Counter::PreparationSteps, UINT64_MAX);
        perf::count(perf::Counter::PreparationSteps); clock_ns = 1000;
    }
    perf::set_sink(sink); perf::flush();
    require(messages.size() == 1 && messages[0].find("schema=1 type=window") != std::string::npos
        && messages[0].find("name=PreparationSteps value=18446744073709551615") != std::string::npos,
        "close flush is one bounded batched sink write and counters saturate");
    perf::flush(true); require(messages.size() == 1, "no empty window/log spam");
    { perf::Span root(perf::Metric::Apply); clock_ns = 2000; }
    perf::flush(); require(messages.size() == 1, "ordinary flush throttled");
    clock_ns = 1000001000; perf::flush(); require(messages.size() == 2, "completed next window drains");
    reset();
    const auto work = [](std::uint64_t generation) {
        for (int i = 0; i < 1000; ++i) {
            perf::Span root(perf::Metric::Apply); perf::annotate({generation});
        }
    };
    std::thread a(work, 1), b(work, 2); a.join(); b.join();
    std::uint64_t accounted = 0;
    while (perf::take_snapshot(s)) accounted += s.roots + s.dropped;
    require(accounted == 2000, "contended roots are counted or explicitly dropped, never silently lost");
    perf::test_reset(); perf::test_clock(nullptr);
    const auto benchmark = [](bool enabled) {
        perf::enable(enabled);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i) {
            perf::Span root(perf::Metric::Apply);
            for (int child = 0; child < 4; ++child) {
                perf::Span helper(perf::Metric::NativeColorSetter);
                perf::count(perf::Counter::SourceRecaptures);
            }
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count() / 10000.0;
    };
    const auto disabled_ns = benchmark(false), enabled_ns = benchmark(true);
    require(perf::take_snapshot(s) && s.roots == 10000
        && stat(s, perf::Metric::NativeColorSetter).count == 40000, "real-clock benchmark retains counts");
    std::cout << "instrumentation benchmark (not in-game): disabled_ns/root=" << disabled_ns
              << " enabled_ns/root=" << enabled_ns << " (root + 4 children; no file logging)\n";
    perf::enable(false);
    if (argc == 2 && std::string(argv[1]) == "--log-fixture") {
        reset(); clock_ns = 1000;
        {
            perf::Span root(perf::Metric::Ready); perf::annotate({7, 9, 11, 13});
            clock_ns = 2000;
            { perf::Span helper(perf::Metric::NativeColorSetter); clock_ns = 4000; }
            perf::sample(perf::Metric::RowPendingDelay, 5000000);
            perf::count(perf::Counter::SourceRecaptures, 2); clock_ns = 10000;
        }
        qualification_tree(7);
        perf::set_sink([](const char* text) noexcept { std::cout << text << '\n'; });
        perf::flush(true); perf::enable(false);
    }
    std::cout << "menu_theme_metrics_selftest passed\n";
}
