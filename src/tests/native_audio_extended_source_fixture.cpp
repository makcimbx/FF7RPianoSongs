// Actual source-row planner/journal; the engine row storage is a test buffer.
#define FF7RP_CHART_PATCH_SELFTEST
#include "game/chart_patch.cpp"
#include <cassert>

namespace ff7r::piano::game {
namespace {
std::array<FrozenStringEntry, 11> composition_row{};
std::array<FrozenStringEntry, 11> composition_original{};
wchar_t carrier_time[]=L"00_00";
FrozenStringEntry carrier_string{reinterpret_cast<uint64_t>(carrier_time),6,6};
uint64_t carrier_note=42;
}
bool composition_patch_source(const SelectionSnapshot& selection, AudioRouteLeaseIdentity lease) {
    composition_row = {};
    composition_row[0]={reinterpret_cast<uint64_t>(&carrier_string),1,1};
    composition_row[2]={reinterpret_cast<uint64_t>(&carrier_note),1,1};
    composition_original = composition_row;
    ChartMemoryLayout layout{};
    layout.chart_base = reinterpret_cast<uintptr_t>(composition_row.data());
    layout.chart_size = sizeof(composition_row); layout.row_header_base = 0;
    layout.time_header_index = 0; layout.monotone_id_header_index = 2;
    PlannedDescriptorChartPatch planned;
    const auto resolve = [](const std::string&, uint64_t& value) { value = 123; return true; };
    ChartNameResolver resolver{resolve,resolve,resolve};
    if (!plan_descriptor_chart_patch(*selection.profile, selection.song->id, layout,
            resolver, planned, nullptr)) return false;
    return apply_planned_descriptor_chart_patch(layout.chart_base, layout.chart_base, selection, lease,
        std::move(planned)) == PlannedChartWriteResult::Applied;
}
bool composition_source_event(std::size_t index,std::string& time,uint64_t& name) {
    if(index>=static_cast<std::size_t>(composition_row[0].num)
        || index>=static_cast<std::size_t>(composition_row[2].num))return false;
    FrozenStringEntry entry{};
    if(!core::safe_copy_bytes(reinterpret_cast<const void*>(composition_row[0].data+index*sizeof(entry)),&entry,sizeof(entry))
        || entry.num<1 || entry.num>32)return false;
    std::array<wchar_t,32> text{};
    if(!core::safe_copy_bytes(reinterpret_cast<const void*>(entry.data),text.data(),entry.num*sizeof(wchar_t)))return false;
    time.clear();for(int i=0;i<entry.num-1;++i) {
        if(text[i]>127)return false;time.push_back(static_cast<char>(text[i]));
    }
    return core::safe_copy_bytes(reinterpret_cast<const void*>(composition_row[2].data+index*sizeof(name)),&name,sizeof(name));
}
bool composition_restore_source() {
    return restore_and_reset_chart_patch_state("composition")
        && std::memcmp(composition_row.data(), composition_original.data(), sizeof(composition_row)) == 0;
}
}
