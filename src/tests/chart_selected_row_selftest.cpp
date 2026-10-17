// Real chart producer/planner/journal with a retained-provider boundary double.
// The consumer re-queries by wrapper FName, matching the actual native parser.
#ifdef NDEBUG
#undef NDEBUG
#endif
#define FF7RP_CHART_PATCH_SELFTEST
#include "game/chart_patch.cpp"
#include <cassert>
#include <iostream>

using namespace ff7r::piano::game;
namespace na=ff7r::piano::game::native_audio;
namespace {
struct Provider {
    ff7rp::pipeline::NativeAudioArtifactSet artifacts;
    std::array<FrozenStringEntry,11> row{};
    std::uint64_t name;
    std::array<std::uint64_t,2> carrier{0x9999,0};
    explicit Provider(std::uint64_t key):name(key) {
        row[0]={reinterpret_cast<std::uint64_t>(carrier.data()),1,1};
        row[2]=row[0];
    }
};
std::array<Provider*,3> providers{};
unsigned captures=0;bool drift_on_second=false;
bool capture(const ff7rp::pipeline::NativeAudioArtifactSet& artifacts,std::uint64_t name,na::NativePianoScoreRow& out) noexcept {
    out={};for(auto* p:providers)if(p && &p->artifacts==&artifacts && p->name==name) {
        out={reinterpret_cast<std::uintptr_t>(p),reinterpret_cast<std::uintptr_t>(p->row.data()),name,1};
        if(drift_on_second && ++captures==2)out.row+=16;
        return true;
    }return false;
}
const Provider& native_lookup(std::uint64_t name) {
    for(auto* p:providers)if(p->name==name)return *p;
    std::abort();
}
SongDifficultyProfile profile(unsigned rows,const char* note) {
    SongDifficultyProfile p;p.difficulty=1;
    for(unsigned i=0;i<rows;++i)p.chart_notes.push_back({"00_"+std::to_string(10+i),note,"",1,0,0,0,0,0,{}});
    return p;
}
bool resolve(const std::string& note,std::uint64_t& value) {
    value=note=="A"?0x1111:note=="B"?0x2222:0;return value!=0;
}
// The qualified frozen PianoScore row can be 4 mod 8. Never form a C++
// FrozenStringEntry/uint64_t reference to its serialized, unaligned storage.
struct FrozenProvider {
    ff7rp::pipeline::NativeAudioArtifactSet artifacts;
    alignas(8) std::array<unsigned char,512> bytes{};
    std::uint64_t name;
    std::uintptr_t row() const {return reinterpret_cast<std::uintptr_t>(bytes.data()+4);}
    template<class T> static T load(std::uintptr_t at) {T value{};std::memcpy(&value,reinterpret_cast<void*>(at),sizeof(value));return value;}
    template<class T> static void store(std::uintptr_t at,const T& value) {std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
    static std::uint64_t relative(std::uintptr_t field,std::uintptr_t target) {
        return (static_cast<std::uint64_t>(static_cast<std::int64_t>(target)-static_cast<std::int64_t>(field))<<1)|1;
    }
    static std::uintptr_t decode(std::uintptr_t field,std::uint64_t data) {
        if(!(data&1))return static_cast<std::uintptr_t>(data);
        std::int64_t displacement=0;std::memcpy(&displacement,&data,sizeof(data));
        return field+static_cast<std::uintptr_t>(displacement>>1);
    }
    FrozenStringEntry header(std::size_t index) const {return load<FrozenStringEntry>(row()+16*index);}
    void header(std::size_t index,FrozenStringEntry value) {store(row()+16*index,value);}
    explicit FrozenProvider(std::uint64_t key):name(key) {
        bytes.fill(0xa5);std::array<FrozenStringEntry,11> empty{};store(row(),empty);
        header(0,{relative(row(),row()+192),1,1});
        header(2,{relative(row()+32,row()+208),1,1});
        const wchar_t text[]=L"01_23";std::memcpy(reinterpret_cast<void*>(row()+176),text,sizeof(text));
        // Negative relative offset from the inner FString to its text.
        store(row()+192,FrozenStringEntry{relative(row()+192,row()+176),6,6});
        store(row()+208,std::uint64_t{0x9999});assert((row()&7)==4);
    }
    std::uint64_t first_note() const {return load<std::uint64_t>(decode(row()+32,header(2).data));}
    const wchar_t* first_time() const {
        const auto entry=decode(row(),header(0).data);
        return reinterpret_cast<const wchar_t*>(decode(entry,load<FrozenStringEntry>(entry).data));
    }
};
std::array<FrozenProvider*,2> frozen_providers{};
unsigned frozen_captures=0;enum class FrozenDrift {None,Generation,Headers,RecaptureFailure,Unreadable};
FrozenDrift frozen_drift=FrozenDrift::None;std::uintptr_t frozen_row_override=0;
void* frozen_protection=nullptr;std::size_t frozen_protection_size=0;
bool capture_frozen(const ff7rp::pipeline::NativeAudioArtifactSet& artifacts,std::uint64_t name,na::NativePianoScoreRow& out) noexcept {
    out={};for(auto* p:frozen_providers)if(&p->artifacts==&artifacts && p->name==name) {
        out={reinterpret_cast<std::uintptr_t>(p),frozen_row_override?frozen_row_override:p->row(),name,1};
        if(++frozen_captures==2) {
            if(frozen_drift==FrozenDrift::Generation)++out.cohort_generation;
            if(frozen_drift==FrozenDrift::Headers){auto h=p->header(0);++h.max;p->header(0,h);}
            if(frozen_drift==FrozenDrift::RecaptureFailure){out={};return false;}
            if(frozen_drift==FrozenDrift::Unreadable) {
                DWORD previous=0;
                if(!VirtualProtect(frozen_protection,frozen_protection_size,PAGE_NOACCESS,&previous))std::abort();
            }
        }
        return true;
    }return false;
}
void frozen_row_regression() {
    FrozenProvider a(0x400),b(0x500);frozen_providers={&a,&b};g_test_capture_pianoscore_row=capture_frozen;
    const auto original_a=a.bytes,original_b=b.bytes;
    SongDescriptor song;auto p=profile(3,"A");SelectionSnapshot selection{};selection.song=&song;selection.profile=&p;
    auto select=[&](FrozenProvider& owner) {song.native_audio=std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet>(&owner.artifacts,[](auto*){});};
    select(a);na::NativePianoScoreRow source;ChartMemoryLayout layout;std::string reason;
    auto qualify=[&](std::uint64_t key) {frozen_captures=0;return resolve_selected_chart_layout(selection,&key,source,layout,reason);};
    assert(a.first_note()==0x9999 && std::wcscmp(a.first_time(),L"01_23")==0);
    ChartNameResolver resolver{resolve,resolve,resolve};
    auto run=[&](FrozenProvider& owner,unsigned count,const char* note) {
        select(owner);p=profile(count,note);assert(qualify(owner.name));
        assert(source.row==owner.row() && layout.chart_size==0xb0 && layout.row_header_base==0);
        PlannedDescriptorChartPatch planned;assert(plan_descriptor_chart_patch(p,"frozen",layout,resolver,planned,&reason));
        assert(apply_planned_descriptor_chart_patch(source.provider,source.row,{},{},std::move(planned))==PlannedChartWriteResult::Applied);
        // Native-shaped consumer re-queries by full selected FName, then decodes
        // both frozen-relative and journal-published absolute array/string data.
        const auto* consumed=owner.name==a.name?frozen_providers[0]:frozen_providers[1];
        assert(consumed->header(0).num==static_cast<int>(count) && consumed->header(2).num==static_cast<int>(count));
        assert(consumed->first_note()==(std::string(note)=="A"?0x1111u:0x2222u));
        assert(std::wcscmp(consumed->first_time(),L"00_10")==0);
        const auto& before=&owner==&a?original_a:original_b;
        assert(std::memcmp(owner.bytes.data(),before.data(),4)==0);
        assert(std::memcmp(owner.bytes.data()+180,before.data()+180,owner.bytes.size()-180)==0);
        assert(restore_and_reset_chart_patch_state("frozen_row_test"));
        assert(a.bytes==original_a && b.bytes==original_b);
        assert(owner.first_note()==0x9999 && std::wcscmp(owner.first_time(),L"01_23")==0);
    };
    run(a,3,"A");run(b,7,"B");run(a,5,"B");run(a,3,"A");select(a);
    assert(!qualify(b.name) && reason=="selected_provider_identity");
    frozen_drift=FrozenDrift::Generation;assert(!qualify(a.name) && reason=="selected_source_generation_drift");frozen_drift=FrozenDrift::None;
    frozen_drift=FrozenDrift::Headers;assert(!qualify(a.name) && reason=="selected_row_header_mismatch");frozen_drift=FrozenDrift::None;a.bytes=original_a;
    frozen_drift=FrozenDrift::RecaptureFailure;assert(!qualify(a.name) && reason=="selected_provider_recapture");frozen_drift=FrozenDrift::None;
    frozen_row_override=a.row()+2;assert(!qualify(a.name) && reason=="selected_row_pointer_alignment4");
    frozen_row_override=4;assert(!qualify(a.name) && reason=="selected_row_pointer_low");
    frozen_row_override=UINTPTR_MAX-3;assert(!qualify(a.name) && reason=="selected_row_bounds");frozen_row_override=0;
    auto rejected_header=[&](FrozenStringEntry header,const char* expected) {
        a.header(0,header);assert(!qualify(a.name) && reason.starts_with(expected));a.bytes=original_a;
    };
    auto malformed=a.header(0);malformed.max=-1;rejected_header(malformed,"selected_row_capacity index=0 count=1 capacity=-1");
    malformed=a.header(0);malformed.num=-1;rejected_header(malformed,"selected_row_count index=0 count=-1");
    malformed=a.header(0);malformed.max=kMaxPatchedChartRows*3+1;rejected_header(malformed,"selected_row_capacity_limit index=0");
    malformed=a.header(0);malformed.data=0;rejected_header(malformed,"selected_row_data_missing index=0");
    rejected_header({},"selected_row_time_empty");
    SYSTEM_INFO info{};GetSystemInfo(&info);const std::size_t page=info.dwPageSize;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(pages);
    DWORD previous=0;assert(VirtualProtect(pages+page,page,PAGE_NOACCESS,&previous));
    // The first 172 bytes are readable, but the required whole 0xb0 row is not.
    std::memcpy(pages+page-172,reinterpret_cast<void*>(a.row()),172);
    frozen_row_override=reinterpret_cast<std::uintptr_t>(pages+page-172);
    assert(!qualify(a.name) && reason=="selected_row_read");
    std::memcpy(pages,a.bytes.data(),a.bytes.size());frozen_row_override=reinterpret_cast<std::uintptr_t>(pages+4);
    frozen_protection=pages;frozen_protection_size=page;frozen_drift=FrozenDrift::Unreadable;
    assert(!qualify(a.name) && reason=="selected_row_reread");frozen_drift=FrozenDrift::None;
    assert(VirtualFree(pages,0,MEM_RELEASE));frozen_row_override=0;frozen_protection=nullptr;
    assert(qualify(a.name));PlannedDescriptorChartPatch planned;
    assert(plan_descriptor_chart_patch(p,"frozen_rollback",layout,resolver,planned,&reason));
    auto* drift=reinterpret_cast<unsigned char*>(planned.entries[4].address);*drift^=1;const auto drifted=a.bytes;
    assert(apply_planned_descriptor_chart_patch(source.provider,source.row,{},{},std::move(planned))==PlannedChartWriteResult::RolledBack);
    assert(a.bytes==drifted);a.bytes=original_a;
    assert(!g_chart_patch_journal.active && a.bytes==original_a && b.bytes==original_b);
}
}
int main() {
    Provider stock(0x100),a(0x200),b(0x300);providers={&stock,&a,&b};g_test_capture_pianoscore_row=capture;
    const auto stock_original=stock.row,a_original=a.row,b_original=b.row;
    ChartNameResolver resolver{resolve,resolve,resolve};
    SongDescriptor song;
    auto run=[&](Provider& selected,unsigned count,const char* note) {
        auto p=profile(count,note);song.native_audio=std::shared_ptr<const ff7rp::pipeline::NativeAudioArtifactSet>(&selected.artifacts,[](auto*){});
        SelectionSnapshot selection{};selection.song=&song;selection.profile=&p;
        na::NativePianoScoreRow source;ChartMemoryLayout layout;std::string reason;std::uint64_t wrapper=selected.name;
        assert(resolve_selected_chart_layout(selection,&wrapper,source,layout,reason));
        assert(layout.chart_base==reinterpret_cast<std::uintptr_t>(selected.row.data()) && layout.row_header_base==0);
        PlannedDescriptorChartPatch planned;assert(plan_descriptor_chart_patch(p,"selected",layout,resolver,planned,&reason));
        assert(apply_planned_descriptor_chart_patch(source.provider,source.row,{},{},std::move(planned))==PlannedChartWriteResult::Applied);
        // No fabricated audio lease: exercise only the real scoped chart writer.
        const auto& consumed=native_lookup(wrapper);
        assert(consumed.row[0].num==static_cast<int>(count) && consumed.row[2].num==static_cast<int>(count));
        const auto* notes=reinterpret_cast<const std::uint64_t*>(consumed.row[2].data);
        assert(notes[0]==(std::string(note)=="A"?0x1111u:0x2222u));
        const auto* times=reinterpret_cast<const FrozenStringEntry*>(consumed.row[0].data);
        assert(std::wcscmp(reinterpret_cast<const wchar_t*>(times[0].data),L"00_10")==0);
        assert(std::memcmp(stock.row.data(),stock_original.data(),sizeof(stock.row))==0);
        assert(restore_and_reset_chart_patch_state("selected_row_test"));
        assert(std::memcmp(a.row.data(),a_original.data(),sizeof(a.row))==0);
        assert(std::memcmp(b.row.data(),b_original.data(),sizeof(b.row))==0);
    };
    run(a,3,"A");run(b,7,"B");run(a,5,"B");run(a,3,"A");
    auto p=profile(2,"A");SelectionSnapshot selection{};selection.song=&song;selection.profile=&p;
    na::NativePianoScoreRow source;ChartMemoryLayout layout;std::string reason;
    std::uint64_t wrong=b.name;assert(!resolve_selected_chart_layout(selection,&wrong,source,layout,reason));
    drift_on_second=true;std::uint64_t changed=a.name;
    assert(!resolve_selected_chart_layout(selection,&changed,source,layout,reason));drift_on_second=false;
    std::uint64_t none=stock.name;assert(!resolve_selected_chart_layout({},&none,source,layout,reason));
    a.row[0].max=-1;std::uint64_t key=a.name;
    assert(!resolve_selected_chart_layout(selection,&key,source,layout,reason));a.row=a_original;
    assert(resolve_selected_chart_layout(selection,&key,source,layout,reason));
    PlannedDescriptorChartPatch planned;assert(plan_descriptor_chart_patch(p,"rollback",layout,resolver,planned,&reason));
    assert(planned.entries.size()>4);
    auto* drift=reinterpret_cast<unsigned char*>(planned.entries[4].address);*drift^=1;
    const auto drifted=a.row;
    assert(apply_planned_descriptor_chart_patch(source.provider,source.row,{},{},std::move(planned))==PlannedChartWriteResult::RolledBack);
    assert(std::memcmp(a.row.data(),drifted.data(),sizeof(a.row))==0);a.row=a_original;
    assert(!g_chart_patch_journal.active);
    assert(std::memcmp(stock.row.data(),stock_original.data(),sizeof(stock.row))==0);
    frozen_row_regression();
    std::cout << "chart selected row selftest passed\n";
}
