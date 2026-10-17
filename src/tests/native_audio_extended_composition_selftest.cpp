// Actual adapter commit -> source journal -> extended transaction composition.
// Only engine entries/TLS transport are doubled; no authority/readiness stubs.
#define FF7RP_ADAPTER_FIXTURE_ONLY
#include "native_audio_adapter_selftest.cpp"
// The included overlay header exposes a different same-named POD.
void* extended_composition_return_address() noexcept;
#define _ReturnAddress extended_composition_return_address
#define FNameValue ExtendedCompositionFName
#include "game/extended_chart.cpp"
#undef FNameValue
#undef _ReturnAddress
#include <cstdio>

namespace ff7r::piano::game {
bool composition_patch_source(const SelectionSnapshot&, AudioRouteLeaseIdentity);
bool composition_restore_source();
bool composition_source_event(std::size_t,std::string&,uint64_t&);
namespace { ChartAudioExpandTlsSnapshot composition_tls{}; }
ChartAudioExpandTlsSnapshot current_chart_audio_expand_tls() noexcept { return composition_tls; }
}

namespace {
std::vector<unsigned char> parser_events;
unsigned tail_constructors=0;
void* __fastcall extended_name(ExtendedCompositionFName* out,const wchar_t*,int mode) {
    assert(mode==0);*out={123,0};return out;
}
void __fastcall parser_reserve(EventHeader* header,int count) {
    assert(count==g_transaction.native_event_count);
    parser_events.assign(static_cast<std::size_t>(g_transaction.expected_capacity)*kEventSize,0);
    *header={parser_events.data(),0,g_transaction.expected_capacity};
}
void* __fastcall event_construct(void* event,void* chart,void* side,uint32_t ordinal,
    float time,float strength,uint8_t note_type,uint8_t dot_type,uint64_t name) {
    ++tail_constructors;
    write_at(event,0,chart);write_at(event,8,side);write_at(event,0x20,ordinal);
    write_at(event,0x24,time);write_at(event,0x28,strength);write_at(event,0x2c,name);
    write_at(event,0x4b,note_type);write_at(event,0x4c,dot_type);return event;
}
void __fastcall event_destruct(void* event) {std::memset(event,0,kEventSize);}
void* __fastcall event_callback(void* callback,void** chart) {
    const auto module=reinterpret_cast<uintptr_t>(g_module);
    write_at(callback,0,module+rva::PianoEventResultCallback);
    write_at(callback,0x20,module+rva::PianoEventCallbackVtable);
    write_at(callback,0x28,*chart);return callback;
}
void __fastcall event_link(void* root,void* child) {
    void* head=nullptr;assert(read_at(root,0x18,head));
    write_at(child,0x10,root);write_at(child,0x18,head);write_at(root,0x18,child);
}
SongDifficultyProfile extended_profile(int difficulty) {
    SongDifficultyProfile p;p.difficulty=difficulty;p.title=L"extended";
    for(unsigned i=0;i<static_cast<unsigned>(513+difficulty);++i) {
        SongChartNote note;note.time_str=(i<10?"0":"")+std::to_string(i)+"_00";note.monotone_id="A";
        if(i<2)note.group_index=1;
        if(i<512)p.chart_notes.push_back(note);else p.extended_chart_tail_notes.push_back(note);
    }
    std::vector<ff7rp::pipeline::ChartEventRow> rows;
    for(const auto& n:p.chart_notes)rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(n));
    for(const auto& n:p.extended_chart_tail_notes)rows.push_back(ff7rp::pipeline::chart_event_row_from_compiled(n));
    const auto plan=ff7rp::pipeline::derive_chart_event_plan(rows);assert(plan.valid());
    p.source_row_count=plan.source_row_count;p.native_prefix_event_count=plan.native_prefix_event_count;
    p.native_event_count=plan.native_event_count;p.required_action_count=plan.required_action_count;
    p.note_count=static_cast<int>(plan.required_action_count);p.physical_chart_digest=plan.physical_digest;
    p.diagnostic_descriptor_hash=77;p.diagnostic_policy_generation=ff7rp::pipeline::chart_row_policy_generation();
    return p;
}
}
void* extended_composition_return_address() noexcept {
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(g_module)+rva::PianoEventVectorReserveCall+5);
}
int main() {
    ff7r::piano::core::set_log_path(L"build/extended-admission/extended-composition.log");
    ff7rp::pipeline::configure_chart_row_limit(true,true);
    AdapterFixture f;f.ready();
    std::array<unsigned char,512> wrapper{};std::array<unsigned char,64> setup{};
    int right=1,left=2,control=3;
    f.wrapper=wrapper.data();
    f.put(reinterpret_cast<na::Address>(menu_snapshot.controller)+runtime_layouts::PianoChartController::chart,f.wrapper);
    f.put(reinterpret_cast<na::Address>(menu_snapshot.controller)+runtime_layouts::PianoChartController::chart_control_block,static_cast<void*>(&control));
    write_at(wrapper.data(),runtime_layouts::PianoScoreWrapper::controller_capture,menu_snapshot.controller);
    write_at(setup.data(),kRightSideOffset,static_cast<void*>(&right));
    write_at(setup.data(),kLeftSideOffset,static_cast<void*>(&left));
    g_module=reinterpret_cast<HMODULE>(f.base);g_api.fname=&extended_name;g_admissions=true;
    g_api.construct=&event_construct;g_api.destruct=&event_destruct;g_api.callback_build=&event_callback;
    g_api.event_link=&event_link;
    g_original_reserve=&parser_reserve;g_reserve_gate.open();
    auto storage=std::make_shared<SongRegistryStorage>(*f.prepared.storage);
    auto& small=(*storage)[0].profiles[0];
    small.chart_notes.push_back({"00_00","A"});
    (*storage)[1].profiles[0]=extended_profile(1);
    (*storage)[1].profiles[1]=extended_profile(2);
    f.prepared.storage=storage;assert(f.adopt());
    SelectionAudioAdmissionAuthority old;
    const auto expand=[&](int visible,int profile,bool revoke=false) {
    invalidate_extended_chart_commit("next_expand");
    write_at(wrapper.data(),kHeaderOffset,EventHeader{});
    auto admission=begin_selection_audio_admission(f.claim(profile,visible),true);assert(admission);
    SelectionAudioAdmissionAuthority authority;assert(capture_selection_audio_admission_authority(admission,authority));
    assert(selection_audio_admission_authority_matches(authority)); // precommit real guard
    auto bad=authority;++bad.activation_generation;SelectionAudioAuthorityDiagnostic diagnostic;
    assert(!selection_audio_admission_authority_matches(bad,&diagnostic));
    assert(diagnostic.failure==SelectionAudioAuthorityFailure::Activation);
    assert(composition_patch_source(authority.selection,selection_audio_admission_lease(admission)));
    assert(commit_selection_audio_admission(admission).status==AudioRouteArmStatus::Armed);
    assert(!registry().selection_guard_matches(authority.selection,authority.token));
    assert(registry().playback_snapshot().token==authority.token);
    bad=authority;++bad.route_lifecycle_epoch;
    assert(!selection_audio_admission_authority_matches(bad,&diagnostic));
    assert(diagnostic.failure==SelectionAudioAuthorityFailure::RouteEpoch);
    bad=authority;++bad.preparation_ordinal;
    assert(!selection_audio_admission_authority_matches(bad,&diagnostic));
    assert(diagnostic.failure==SelectionAudioAuthorityFailure::PreparationOrdinal);
    bad=authority;++bad.token.route_generation;
    assert(!selection_audio_admission_authority_matches(bad,&diagnostic));
    assert(diagnostic.failure==SelectionAudioAuthorityFailure::ActiveToken);
    bad=authority;++bad.selection.profile_index;
    assert(!selection_audio_admission_authority_matches(bad,&diagnostic));
    assert(diagnostic.failure==SelectionAudioAuthorityFailure::ActiveSelection);
    // Same producer order as chart_patch -> note_count, not a precommit matcher test.
    composition_tls={};composition_tls.active=true;composition_tls.depth=1;composition_tls.original_inflight=true;
    composition_tls.generation=authority.activation_generation;composition_tls.selection_generation=authority.selection.generation;
    composition_tls.route_generation=authority.token.route_generation;composition_tls.lease_generation=authority.token.lease_generation;
    composition_tls.song_key=authority.token.song_key;
    assert(selection_audio_admission_authority_matches(authority));
    if(old.exact)assert(!selection_audio_admission_authority_matches(old));
    if(revoke) {
        assert(registry().revoke_playback(authority.token));
        assert(!selection_audio_admission_authority_matches(authority,&diagnostic));
        assert(diagnostic.failure==SelectionAudioAuthorityFailure::PlaybackSelection);
        assert(!begin_extended_chart_transaction(composition_tls,authority,f.wrapper,setup.data(),rva::PersistentChartExpandCaller));
        assert(!g_transaction.active && !g_global_claim);
        assert(composition_restore_source());assert(cancel_selection_audio_admission(admission));
        composition_tls={};old=authority;return;
    }
    if(visible==13) {
        assert(!begin_extended_chart_transaction(composition_tls,authority,f.wrapper,setup.data(),rva::PersistentChartExpandCaller));
        assert(!g_transaction.active);assert(composition_restore_source());
        assert(cancel_selection_audio_admission(admission));composition_tls={};old=authority;return;
    }
    // Parentless actions/UI count need not equal expanded native event count.
    assert(authority.selection.profile->note_count!=static_cast<int>(authority.selection.profile->native_event_count));
    assert(begin_extended_chart_transaction(composition_tls,authority,f.wrapper,setup.data(),rva::PersistentChartExpandCaller));
    auto* header=reinterpret_cast<EventHeader*>(wrapper.data()+kHeaderOffset);
    reserve_detour(header,512);assert(g_transaction.reserve_hit && !g_transaction.reserve_mismatch);
    write_at(wrapper.data(),kFrameRateOffset,60.0f);
    float prefix_max=0;
    for(int i=0;i<g_transaction.prefix_event_count;++i) {
        const auto& plan=g_transaction.plans[static_cast<std::size_t>(i)];float time=0;
        std::string source_time;uint64_t source_name=0;
        assert(composition_source_event(static_cast<std::size_t>(i),source_time,source_name));
        assert(source_time==plan.note->time_str && source_name==plan.fname);
        assert(parse_time(source_time,60.0f,time));
        void* slot=parser_events.data()+static_cast<std::size_t>(i)*kEventSize;
        event_construct(slot,f.wrapper,&right,plan.entry.ordinal,time,0,plan.note_type,plan.dot_type,source_name);
        void* chart=f.wrapper;event_callback(static_cast<unsigned char*>(slot)+0x50,&chart);prefix_max=time;
    }
    header->count=g_transaction.prefix_event_count;write_at(wrapper.data(),kMaxTimeOffset,prefix_max);
    const auto prefix_constructors=tail_constructors;
    composition_tls={}; // actual note_count ends the original-call TLS before finish
    assert(composition_restore_source());
    assert(finish_extended_chart_transaction(f.wrapper,setup.data(),rva::PersistentChartExpandCaller));
    assert(header->count==static_cast<int>(authority.selection.profile->native_event_count));
    assert(tail_constructors-prefix_constructors==authority.selection.profile->native_event_count-512);
    assert(cancel_selection_audio_admission(admission));
    assert(!selection_audio_admission_authority_matches(authority));old=authority;
    };
    expand(13,0);expand(14,0);expand(13,0);expand(14,1);expand(14,1);expand(14,0,true);
    // Ordinary stock expansion has no custom authority and cannot reserve tails.
    assert(!begin_extended_chart_transaction({}, {},f.wrapper,setup.data(),rva::PersistentChartExpandCaller));
    assert(!g_transaction.active && !g_global_claim);
    assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
    invalidate_extended_chart_commit("test_complete");
    std::puts("native audio extended composition selftest passed");
}
