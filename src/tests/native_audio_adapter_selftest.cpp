// Runs the actual adapter/catalog/registry against the actual native backend
// callbacks, mapped readers and real menu authority. Native functions are doubled.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game/audio_sead.h"
#include "game/audio_production_wiring.h"
#include "game/scoreinfo_overlay.h"
#include "game/menu_session_authority.h"
#define main native_backend_regression_main
#include "native_audio_runtime_selftest.cpp"
#undef main
#include "game/audio_sead.cpp"
#include "game/audio_sidecar_runtime.cpp"

namespace {
MenuSessionSnapshot menu_snapshot;
bool scoreinfo_retirement=true;
}
namespace ff7r::piano::game {
bool capture_live_uobject_handle(void* object,UObjectLiveHandle& handle) {
    gu::UObjectLiveHandle captured{};
    if(!current || !gu::capture_live_handle(current->access(),reinterpret_cast<na::Address>(object),captured))return false;
    handle={captured.internal_index,captured.serial_number};return true;
}
bool validate_live_uobject_handle(void* object,const UObjectLiveHandle& handle) {
    return current && gu::validate_live_handle(current->access(),reinterpret_cast<na::Address>(object),{handle.internal_index,handle.serial_number});
}
bool resolve_piano_menu_widget_binding(void* list,void*& widget,UObjectLiveHandle& identity) noexcept {
    widget=nullptr;na::Address object=0;
    return core::safe_read_field(list,0x7a0,identity)
        && gu::read_uobject_item_pointer(current->access(),identity.internal_index,object)
        && (widget=reinterpret_cast<void*>(object)) && validate_live_uobject_handle(widget,identity);
}
bool invalidate_scoreinfo_playback(const CustomContextToken&) {return scoreinfo_retirement;}
}

struct AdapterFixture : RuntimeFixture {
    std::unique_ptr<ProgressiveAudioCatalogBuilder> builder;
    std::shared_ptr<const PreparedAudioPrefix> prefix;
    PreparedAudioCatalog prepared;
    SelectionActivationListContext context{};
    void* wrapper=nullptr;
    explicit AdapterFixture(bool publish_menu=true) {
        using namespace ff7r::piano::game;
        ff7r::piano::game::installed=false;ff7r::piano::game::disabled=false;
        unresolved_mutation=false;catalog={};active={};reservation={};
        machine=SelectionActivationReservationMachine{};catalog_generation=0;catalog_revision=registry().registry_snapshot().catalog_revision;
        scoreinfo_retirement=true;
        assert(install_audio_sead_hooks({}));
        SongDescriptor song; song.id="adapter-fixture";song.native_audio=artifacts; song.visible_index=13;
        song.profiles.resize(2);
        song.profiles[0].title=L"normal";song.profiles[1].title=L"hard";
        song.profiles[0].difficulty=1;song.profiles[1].difficulty=2;
        builder=std::make_unique<ProgressiveAudioCatalogBuilder>();prefix=builder->append(song);assert(prefix);
        assert(!builder->append(song));assert(builder->snapshot()==prefix);
        auto second=song;second.id="adapter-second";second.visible_index=14;
        prefix=builder->append(second);assert(prefix);
        auto storage=std::make_shared<const SongRegistryStorage>(std::vector<SongDescriptor>{song,second});
        assert(prepare_audio_catalog_from_prefix(storage,prefix,prepared));
        assert(audio_sead_detail::enqueue_prepared_audio_prefix(*prefix));
        const auto widget=object(name("AdapterWidget"),seeds[0].stock.objects[1],0,9);
        std::vector<unsigned char> controller_bytes(4096);
        const auto controller=reinterpret_cast<na::Address>(controller_bytes.data());
        memory.emplace(controller,std::move(controller_bytes));
        // Real menu producers derive a native controller from the embedded list;
        // only its widget has a GU identity (PianoMenuSessionEvidence-20260729).
        wrapper=reinterpret_cast<void*>(alloc(64));put(controller+runtime_layouts::PianoChartController::chart,wrapper);
        context={reinterpret_cast<void*>(widget),this->index(widget),9};
        menu_snapshot={};
        menu_snapshot.controller=reinterpret_cast<void*>(controller);menu_snapshot.widget=context.object;
        menu_snapshot.list=reinterpret_cast<void*>(controller+0x6f0);
        menu_snapshot.widget_identity={context.internal_index,context.serial_number};
        put(controller+0x6f0+0x7a0,menu_snapshot.widget_identity);
        open_menu(publish_menu);
    }
    ~AdapterFixture() {
        using namespace ff7r::piano::game;
        assert(shutdown_audio_sead());catalog={};prepared={};prefix.reset();builder.reset();
        registry().replace({});(void)menu_session_authority().retire(menu_snapshot.generation);menu_snapshot={};
    }
    void open_menu(bool publish=true) {
        put(reinterpret_cast<na::Address>(menu_snapshot.list)+0x7d8,std::uint16_t{0});
        menu_snapshot=menu_session_authority().begin_open(menu_snapshot.controller,menu_snapshot.list,
            context.object,{context.internal_index,context.serial_number},0,MenuListOwnership::Managed);
        assert(menu_snapshot);
        // Original Open writes the active word before reflected selection calls.
        put(reinterpret_cast<na::Address>(menu_snapshot.list)+0x7d8,std::uint16_t{1});
        if(publish)publish_menu_ready();
    }
    void publish_menu_ready() {
        assert(menu_session_authority().publish_ready(menu_snapshot,1,context.object,
            {context.internal_index,context.serial_number}));
        menu_snapshot=capture_ready_menu_session();assert(menu_snapshot);
    }
    bool adopt() {
        const auto expected=registry().registry_snapshot();
        AudioProductionCatalogScope scope;
        auto registry_commit=registry().begin_catalog_commit(expected,prepared.storage);assert(registry_commit);
        auto audio_commit=begin_prepared_audio_catalog_commit(prepared,expected.catalog_revision,scope);
        if(!audio_commit)return false;
        registry().commit_catalog(*registry_commit);
        commit_prepared_audio_catalog(*audio_commit,expected.generation+1,expected.catalog_revision+1);
        finalize_prepared_audio_catalog_commit(*audio_commit);registry_commit.reset();return true;
    }
    void ready() {immediate=true;na::factory_hook();frame();frame();frame();
        assert(na::native_audio_status(*work).stage==na::RuntimeStage::Ready);}
    SelectionActivationClaim claim(int profile=0,int visible=13,bool take_claim=true) {
        if(!capture_ready_menu_session())open_menu();
        registry().set_active_selection(visible,0);
        SelectionSnapshot selection=registry().selection_snapshot(), initialized;
        assert(registry().initialize_profile_if_absent(selection,profile,initialized));selection=initialized;
        if(selection.profile_index!=profile) {
            assert(registry().cycle_active_profile(profile-selection.profile_index));selection=registry().selection_snapshot();
        }
        assert(selection.profile_index==profile);
        assert(reserve_selection_audio_activation(selection,context));
        const auto handoff=prepare_selection_audio_activation_handoff(selection,context);assert(handoff);
        SelectionSnapshot published;
        assert(publish_selection_audio_activation_handoff(handoff,context,visible,0,true,published)==SelectionActivationHandoffResult::Confirmed);
        const auto repeated=prepare_selection_audio_activation_handoff(published,context);
        assert(repeated.result==SelectionActivationHandoffResult::AlreadyConfirmed);
        assert(confirm_selection_audio_activation_handoff(repeated,published,context)==SelectionActivationHandoffResult::Confirmed);
        assert(registry().selection_snapshot().generation==published.generation);
        const auto closing=menu_session_authority().begin_close(menu_snapshot.list);assert(closing);
        put(reinterpret_cast<na::Address>(menu_snapshot.list)+0x7d8,std::uint16_t{0x100});
        const auto terminal=terminate_selection_audio_activation_for_menu_session(closing,published,SelectionActivationTerminalReason::ListClose);
        assert(terminal.decision==SelectionActivationTerminalDecision::Preserved);
        assert(menu_session_authority().finish_close(closing,0x100));
        if(!take_claim)return {};
        return claim_selection_audio_activation(wrapper,rva::PersistentChartExpandCaller);
    }
};

#ifndef FF7RP_ADAPTER_FIXTURE_ONLY
int main() {
    {AdapterFixture f(false);f.ready();assert(f.adopt());registry().set_active_selection(13,0);
     const auto selection=registry().selection_snapshot();std::uint64_t native_name=0;
     assert(selected_native_audio_name(selection,native_name));
     UObjectLiveHandle controller{};
     assert(!capture_live_uobject_handle(menu_snapshot.controller,controller));
     SelectionActivationReservationDiagnostic diagnostic;unsigned originals=0;
     auto query=[&] {
         return selection_activation_query_exact_once([&]{++originals;return true;},true,true,[]{assert(false);},
             [&]{return reserve_selection_audio_activation(selection,f.context,&diagnostic);});
     };
     assert(!query() && originals==1 && diagnostic.failure==SelectionActivationReservationFailure::MenuNotReady);
     assert(machine.state()==SelectionActivationReservationState::Empty);
     f.publish_menu_ready();
     const auto weak=reinterpret_cast<na::Address>(menu_snapshot.list)+0x7a0;
     f.put(weak,UObjectLiveHandle{f.context.internal_index,f.context.serial_number+1});
     assert(!query() && originals==2 && diagnostic.failure==SelectionActivationReservationFailure::NativeListBinding);
     f.put(weak,menu_snapshot.widget_identity);
     assert(query() && originals==3 && diagnostic.failure==SelectionActivationReservationFailure::None);
     const auto generation=diagnostic.reservation_generation;
     assert(query() && originals==4 && diagnostic.reservation_generation==generation);
     assert(diagnostic.menu_generation==menu_snapshot.generation);
     const auto stale=prepare_selection_audio_activation_handoff(selection,f.context);assert(stale);
     assert(menu_session_authority().retire(menu_snapshot.generation));f.open_menu();
     assert(!query() && diagnostic.failure==SelectionActivationReservationFailure::ReservationDrift);
     assert(reclaim_selection_audio_activation_for_controller(menu_snapshot.controller,SelectionActivationTerminalReason::Reopen));
     assert(query() && diagnostic.reservation_generation>generation);
     assert(confirm_selection_audio_activation_handoff(stale,selection,f.context)==SelectionActivationHandoffResult::ReservationGenerationDrift);
     assert(revoke_selection_audio_activation_if_pending());}
    {AdapterFixture f;f.ready();assert(f.adopt());(void)f.claim(0,13,false);
     assert(!claim_selection_audio_activation(reinterpret_cast<void*>(1),rva::PersistentChartExpandCaller));
     assert(reclaim_selection_audio_activation_for_controller(menu_snapshot.controller,SelectionActivationTerminalReason::ControllerDestructor));
     assert(!claim_selection_audio_activation(f.wrapper,rva::PersistentChartExpandCaller));}
    {AdapterFixture f;f.ready();assert(f.adopt());(void)f.claim(0,13,false);
     assert(reclaim_selection_audio_activation_for_controller(menu_snapshot.controller,SelectionActivationTerminalReason::Reopen));
     f.open_menu();assert(!claim_selection_audio_activation(f.wrapper,rva::PersistentChartExpandCaller));}
    {AdapterFixture f;
     assert(prepared_audio_catalog_readiness(f.prepared)==AudioCatalogReadiness::Pending);
     assert(!f.adopt());assert(!reserve_selection_audio_activation({},f.context));
     f.ready();assert(prepared_audio_catalog_readiness(f.prepared)==AudioCatalogReadiness::Ready);
     assert(f.adopt());
     // A claim without a complete chart plan unwinds the real frozen profile.
     {auto claim=f.claim();assert(claim);auto admission=begin_selection_audio_admission(std::move(claim),false);assert(!admission);}
     assert(custom_audio_route_idle_for_menu_input());
     // Repeated activation/profile choice reuses genuine native identity without
     // another mount/acquisition/bridge, but receives a fresh exact chart guard.
     for(int profile=0;profile<2;++profile) {
         auto claim=f.claim(profile);assert(claim);
         auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
         SelectionAudioAdmissionAuthority authority;assert(capture_selection_audio_admission_authority(admission,authority));
         assert(authority.selection.profile_index==profile);
         assert(selection_audio_admission_authority_matches(authority));
         assert(!selection_audio_admission_cancellation_complete(authority.selection,selection_audio_admission_lease(admission)));
         auto lease=commit_selection_audio_admission(admission);assert(lease.status==AudioRouteArmStatus::Armed);
          // Commit transfers authority to genuine playback, not cancellation.
          assert(selection_audio_admission_authority_matches(authority));
         assert(registry().playback_snapshot().token==authority.token);
         const auto cleanup=release_audio_route_on_piano_list_return(13);assert(cleanup.released());
          assert(!registry().playback_snapshot());
          assert(!selection_audio_admission_authority_matches(authority));
         assert(selection_audio_admission_cancellation_complete(authority.selection,lease.identity));
     }
     assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
     {auto claim=f.claim(0,14);auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
      assert(commit_selection_audio_admission(admission).status==AudioRouteArmStatus::Armed);
      assert(registry().playback_snapshot().song->id=="adapter-second");
      assert(release_audio_route_on_piano_list_return(14).released());}
     {auto claim=f.claim();auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
      SelectionAudioAdmissionAuthority authority;assert(capture_selection_audio_admission_authority(admission,authority));
      const auto lease=selection_audio_admission_lease(admission);assert(cancel_selection_audio_admission(admission));
      assert(selection_audio_admission_cancellation_complete(authority.selection,lease));}
     auto stale=registry().selection_snapshot();registry().clear_active_selection();std::uint64_t name=0;
     assert(!selected_native_audio_name(stale,name));
    }
    {AdapterFixture f;na::factory_hook();f.put(f.closure+16,std::uint64_t{0});f.frame();
     assert(prepared_audio_catalog_readiness(f.prepared)==AudioCatalogReadiness::Failed);assert(!f.adopt());}
    {AdapterFixture f;f.ready();assert(f.adopt());
     {auto claim=f.claim();assert(claim);
      assert(!release_audio_route_on_piano_list_return(13).released());
      assert(shutdown_audio_sead_with_result().status==AudioRouteShutdownStatus::Retained);}}
    {AdapterFixture f;f.ready();assert(f.adopt());auto claim=f.claim();
     auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
     SelectionAudioAdmissionAuthority authority;assert(capture_selection_audio_admission_authority(admission,authority));
     fail_required_chart_expansion(authority,f.wrapper);
     assert(!cancel_selection_audio_admission(admission));
     assert(!selection_audio_admission_cancellation_complete(authority.selection,selection_audio_admission_lease(admission)));
     assert(shutdown_audio_sead_with_result().status==AudioRouteShutdownStatus::Retained);
     // Only fixture teardown clears this terminal latch; production has no repair API.
     unresolved_mutation=false;assert(cancel_selection_audio_admission(admission));}
    {AdapterFixture f;f.ready();assert(f.adopt());auto claim=f.claim();
     auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
     SelectionAudioAdmissionAuthority authority;assert(capture_selection_audio_admission_authority(admission,authority));
     scoreinfo_retirement=false;assert(!cancel_selection_audio_admission(admission));
     assert(registry().selection_guard_matches(authority.selection,authority.token));
     scoreinfo_retirement=true;assert(cancel_selection_audio_admission(admission));}
    {AdapterFixture f;f.ready();assert(f.adopt());auto claim=f.claim();
     auto admission=begin_selection_audio_admission(std::move(claim),true);assert(admission);
     f.put(f.base+rva::PackageStoreGeneration,std::uint32_t{2});
     assert(commit_selection_audio_admission(admission).status!=AudioRouteArmStatus::Armed);
     assert(cancel_selection_audio_admission(admission));}
    std::cout<<"native audio adapter selftest passed\n";
}
#endif
