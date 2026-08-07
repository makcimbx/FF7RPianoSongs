#include "runtime_lifecycle_test_support.h"

namespace ff7r::piano::tests::runtime_lifecycle {

#define FF7RP_DECLARE_RUNTIME_LIFECYCLE_CASE(name) void name();
FF7RP_RUNTIME_LIFECYCLE_CASES(FF7RP_DECLARE_RUNTIME_LIFECYCLE_CASE)
#undef FF7RP_DECLARE_RUNTIME_LIFECYCLE_CASE

} // namespace ff7r::piano::tests::runtime_lifecycle

namespace {

using RuntimeLifecycleCase = void (*)();

#define FF7RP_RUNTIME_LIFECYCLE_CASE_ENTRY(name) \
    &ff7r::piano::tests::runtime_lifecycle::name,
constexpr RuntimeLifecycleCase kRuntimeLifecycleCases[]{
    FF7RP_RUNTIME_LIFECYCLE_CASES(FF7RP_RUNTIME_LIFECYCLE_CASE_ENTRY)
};
#undef FF7RP_RUNTIME_LIFECYCLE_CASE_ENTRY

} // namespace

int main()
{
    for (const RuntimeLifecycleCase test : kRuntimeLifecycleCases) {
        test();
    }
    return 0;
}
