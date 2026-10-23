#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ff7r::piano::game::menu_theme_metrics {
// Stable diagnostic IDs, not native authority. Times are wall-clock nanoseconds.
#define FF7RP_THEME_METRICS(X) \
    X(OpenHook) X(CloseHook) X(RowSetupHook) X(OwnedRefreshHook) X(SelectionHook) \
    X(OpenOriginal) X(CloseOriginal) X(RowSetupOriginal) X(OwnedRefreshOriginal) X(SelectionOriginal) \
    X(Ready) X(Close) X(Dispatch) X(RowBefore) X(RowAfter) X(SelectionBefore) X(SelectionAfter) \
    X(Prepare) X(Resolve) X(Capture) X(Upload) X(Compose) X(Tint) X(Apply) X(Rollback) \
    X(SuspendRow) X(RefreshRow) X(RetireRow) X(ReconcileSpeed) X(QualifyCallback) \
    X(ValidateSubject) X(ValidateTarget) X(ValidatePalette) X(FindSubject) X(BorrowBrush) \
    X(CaptureMembers) X(ApplyMembers) X(PaletteColorHook) X(PaletteOpacityHook) \
    X(PaletteBind) X(PaletteChange) X(PaletteRestore) X(PaletteRetire) X(PaletteEmit) X(ProjectSource) \
    X(NativeConstruct) X(NativeRoot) X(NativeBrushCopy) X(NativeBrushDestroy) X(NativeBrushAssign) \
    X(NativeTextureUpload) X(NativeStyleCopy) X(NativeStylePublish) X(NativeInvalidate) X(NativeSynchronize) \
    X(NativeColorCopy) X(NativeColorAssign) X(NativeColorDestroy) X(NativeColorSetter) X(NativeOpacitySetter) \
     X(NativeBottomCapture) X(NativeBottomAssign) X(NativeBottomDestroy) \
    X(MemberValidation) X(NativeBottomSetter) X(NativeBrushConstruct) X(NativeBrushPublish) \
    X(NativeBrushSetResource) X(NativeFindChild) X(NativeFindName) X(NativeMaterialize) X(NativeStaticClass) \
    X(NativeStyleConstruct) X(NativeStyleDestroy) X(NativeTextureCreate) X(NativeTextureMark) X(NativeUnlock) \
    X(RowMembership) X(ValidateRooted) X(ValidateTargetBasic) \
    X(QualifyPalette) X(QualifyScope) X(QualifyMapping) \
    X(QualifyRowOwner) X(QualifySelectionOwner) X(QualifyDispatchOwner) \
    X(FactsCapture) X(FactsObjectSnapshot) X(FactsDynamicReads) \
    X(FactsIdentityAccess) X(FactsSessionSnapshot) X(FactsClassify) \
    X(ReadyToCommit) X(RowPendingDelay) X(PreparationQueueWait)
enum class Metric : std::uint8_t {
#define FF7RP_THEME_METRIC_ENUM(name) name,
    FF7RP_THEME_METRICS(FF7RP_THEME_METRIC_ENUM)
#undef FF7RP_THEME_METRIC_ENUM
    Count
};
#define FF7RP_THEME_COUNTERS(X) \
    X(TreeNodes) X(RowMembershipChecks) X(TargetBasicChecks) X(PaletteRouteVisits) \
    X(CaptureHits) X(CaptureMisses) X(ComposeHits) X(ComposeMisses) X(TintHits) X(TintMisses) \
    X(CaptureRequestedMembers) X(ApplyRequestedMembers) X(PublishedMembers) \
    X(PreparationSteps) X(InactivePreparationSteps) X(PreparationYields) X(PreparationRestarts) \
    X(RowRebinds) X(SameAssociationRefreshes) X(RowsPending) X(RowsStyled) X(RowsCancelled) \
    X(ThemeCommits) X(PassiveBindings) X(PaletteForwarded) X(PaletteRejected) X(SourceRecaptures) \
    X(PolicyNoops) X(BrushNoops) X(ClockReads) X(ClockFailures) X(DepthOverflow) X(MalformedScopes) \
    X(CapturePublishedMembers) X(RowMembershipEntries) X(SourceCaptures)
// Publication into rooted capture storage is distinct from publication into a widget.
enum class Counter : std::uint8_t {
#define FF7RP_THEME_COUNTER_ENUM(name) name,
    FF7RP_THEME_COUNTERS(FF7RP_THEME_COUNTER_ENUM)
#undef FF7RP_THEME_COUNTER_ENUM
    Count
};
constexpr std::size_t metric_count = static_cast<std::size_t>(Metric::Count);
constexpr std::size_t counter_count = static_cast<std::size_t>(Counter::Count);
constexpr std::size_t histogram_size = 12;
// Upper bounds in microseconds; final bucket is overflow. No percentile interpolation.
inline constexpr std::array<std::uint64_t, histogram_size - 1> histogram_us{
    50, 250, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000, 512000};
struct Context {
    std::uint64_t generation = 0, setup = 0;
    std::int32_t row = -1, index = -1;
};
struct Stat {
    std::uint64_t count = 0, failed = 0, inclusive_ns = 0, self_ns = 0, max_ns = 0;
    std::array<std::uint64_t, histogram_size> histogram{};
};
struct SlowEvent { Metric metric{}; Context context{}; std::uint64_t ns = 0; };
struct Snapshot {
    std::uint64_t sequence = 0, generation = 0, roots = 0, dropped = 0, started_ns = 0, ended_ns = 0;
    std::array<Stat, metric_count> stats{};
    std::array<std::uint64_t, counter_count> counters{};
    std::array<SlowEvent, 8> slow{};
};
// Trivial explicit scopes also work inside existing SEH wrappers (no C++ unwinding).
struct Stamp {
    Metric metric{};
    std::uint64_t started = 0, children = 0;
    Stamp* parent = nullptr;
    bool active = false;
};
void enable(bool) noexcept;
bool enabled() noexcept;
std::uint64_t now_ns() noexcept;
void begin(Stamp&, Metric) noexcept;
void finish(Stamp&, bool success = true) noexcept;
class Span {
    Stamp stamp_{};
public:
    explicit Span(Metric metric) noexcept { begin(stamp_, metric); }
    ~Span() { finish(stamp_); }
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
};
void count(Counter, std::uint64_t amount = 1) noexcept;
void sample(Metric, std::uint64_t ns) noexcept;
// Copy existing callback facts only; never reread native data for a diagnostic.
void annotate(const Context&) noexcept;
// Called outside native/protected locks by the existing title poll service.
// Sink must not reenter game code; it receives bounded schema-versioned log lines.
using Sink = void (*)(const char*) noexcept;
void set_sink(Sink) noexcept;
void flush(bool force = false) noexcept;
// Pure projection used by tests/tooling; taking a snapshot drains completed roots.
bool take_snapshot(Snapshot&) noexcept;
const char* name(Metric) noexcept;
const char* name(Counter) noexcept;
#ifdef FF7RP_MENU_THEME_METRICS_SELFTEST
void test_clock(std::uint64_t (*clock)() noexcept) noexcept;
void test_reset() noexcept;
#endif
} // namespace ff7r::piano::game::menu_theme_metrics
