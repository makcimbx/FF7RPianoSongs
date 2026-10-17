#include "game/audio_sead.h"
#include "game/audio_production_wiring.h"
#include "game/module_hooks.h"
#include "game/menu_session_authority.h"
#include "game/scoreinfo_overlay.h"
#include "game/uobject_lifetime.h"
#include "game/runtime_layouts.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"
#include "core/pe_image.h"
#include "core/logging.h"
#include "pipeline/cache.h"
#include <atomic>
#include <mutex>
#include <utility>

namespace ff7r::piano::game {
namespace {
std::recursive_mutex state_mutex;
PreparedAudioCatalog catalog;
std::uint64_t catalog_generation=0,catalog_revision=1,next_lease=0,revocation_epoch=1;
bool installed=false,disabled=false,unresolved_mutation=false;
SelectionActivationReservationMachine machine;
struct Reservation {
    SelectionSnapshot selection;
    SelectionActivationListContext context;
    MenuSessionSnapshot menu;
    bool confirmed=false,notified=false;
    SelectionActivationTransferState transfer=SelectionActivationTransferState::Pending;
} reservation;
struct Active {
    SelectionSnapshot selection;
    CustomContextToken token;
    std::uint64_t activation=0;
    void* wrapper=nullptr;
    bool committed=false;
} active;

bool exact(const SelectionSnapshot& a,const SelectionSnapshot& b) noexcept {
    return a && b && a.generation==b.generation && a.storage==b.storage && a.song==b.song
        && a.profile==b.profile && a.profile_index==b.profile_index
        && a.visible_index==b.visible_index && a.base_slot==b.base_slot;
}
bool context_exact(const SelectionActivationListContext& a,const SelectionActivationListContext& b) noexcept {
    return a && b && a.object==b.object && a.internal_index==b.internal_index && a.serial_number==b.serial_number;
}
bool live_context(const SelectionActivationListContext& c) noexcept {
    return c && validate_live_uobject_handle(c.object,{c.internal_index,c.serial_number});
}
bool native_controller_list_exact(const MenuSessionSnapshot& menu) noexcept {
    // PianoMenuSessionEvidence-20260729: controller is a native subobject,
    // list is embedded at +0x6f0; only the list widget has a GU weak identity.
    const auto controller=reinterpret_cast<std::uintptr_t>(menu.controller);
    return controller && controller<=UINTPTR_MAX-0x6f0
        && reinterpret_cast<std::uintptr_t>(menu.list)==controller+0x6f0;
}
bool native_name_locked(const SelectionSnapshot& selection,std::uint64_t& name) noexcept {
    name=0;
    if(!installed || disabled || !selection || !catalog.storage || !catalog.prefix
        || selection.storage.get()!=catalog.storage.get() || !registry().selection_matches(selection))return false;
    const auto* song=audio_sead_detail::find_prepared_native_song(*catalog.prefix,selection.song->id);
    return song && song->artifacts==selection.song->native_audio
        && native_audio::native_audio_selected_name(*song->token,song->generation,name);
}
void revoke_locked() noexcept {
    machine.revoke();reservation={};
    if(revocation_epoch==UINT64_MAX)disabled=true;else ++revocation_epoch;
}
bool thaw_locked(const SelectionSnapshot& frozen,std::uint64_t generation) noexcept {
    SelectionSnapshot thawed;
    return registry().thaw_active_profile_for_activation(frozen,generation,thawed);
}
void abandon_claim(const SelectionSnapshot& selection,std::uint64_t generation) noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.generation()!=generation || !exact(reservation.selection,selection))return;
    if(!thaw_locked(selection,generation)){disabled=true;return;}
    (void)machine.retire_consumed(generation);revoke_locked();
}
bool retire_active_locked() noexcept {
    if(unresolved_mutation)return false;
    if(!active.token.valid())return true;
    const bool guarded=registry().selection_guard_matches(active.selection,active.token);
    const auto playback=registry().playback_snapshot();
    const auto cleanup=registry().cleanup_lease();
    if(!guarded && playback.token!=active.token && cleanup.token!=active.token)return false;
    // A failed retirement must leave the actual chart guard/denial authority
    // intact so retained journals cannot be mistaken for completed rollback.
    if(!invalidate_scoreinfo_playback(active.token))return false;
    if(guarded && !registry().release_selection_guard(active.token))return false;
    if(cleanup.song && cleanup.token==active.token && !registry().retire_cleanup_lease(active.token))return false;
    registry().clear_frozen_profile();
    (void)machine.retire_consumed(active.activation);
    active={};revoke_locked();return true;
}
} // namespace

bool prepare_audio_catalog_from_prefix(std::shared_ptr<const SongRegistryStorage> storage,
    std::shared_ptr<const PreparedAudioPrefix> prefix,PreparedAudioCatalog& prepared) noexcept {
    if(!storage || !prefix || !audio_sead_detail::prepared_audio_prefix_matches(*prefix,*storage))return false;
    prepared={std::move(storage),std::move(prefix)};return true;
}
AudioCatalogReadiness prepared_audio_catalog_readiness(const PreparedAudioCatalog& prepared) noexcept {
    if(!prepared.storage || !prepared.prefix)return AudioCatalogReadiness::Failed;
    return audio_sead_detail::prepared_audio_prefix_readiness(*prepared.prefix);
}
class PreparedAudioCatalogCommit final {
public:
    PreparedAudioCatalog* prepared=nullptr;
    std::unique_lock<std::recursive_mutex> lock;
};
std::shared_ptr<PreparedAudioCatalogCommit> begin_prepared_audio_catalog_commit(PreparedAudioCatalog& prepared,
    std::uint64_t expected,const AudioProductionCatalogScope& scope) noexcept {
    if(!scope || prepared_audio_catalog_readiness(prepared)!=AudioCatalogReadiness::Ready)return {};
    try {
        auto commit=std::make_shared<PreparedAudioCatalogCommit>();
        commit->lock=std::unique_lock(state_mutex,std::try_to_lock);
        if(!commit->lock || !installed || disabled || active.token.valid()
            || machine.state()==SelectionActivationReservationState::Reserved
            || machine.state()==SelectionActivationReservationState::Consumed || catalog_revision!=expected)return {};
        commit->prepared=&prepared;return commit;
    }catch(...){return {};}
}
void commit_prepared_audio_catalog(PreparedAudioCatalogCommit& commit,std::uint64_t generation,std::uint64_t revision) noexcept {
    catalog=*commit.prepared;catalog_generation=generation;catalog_revision=revision;
}
void finalize_prepared_audio_catalog_commit(PreparedAudioCatalogCommit& commit) noexcept {
    *commit.prepared={};if(commit.lock)commit.lock.unlock();
}
// Wiring still brackets non-native adapter calls; there is no borrower/native
// transition observer to drain after removal of the replacement audio hooks.
void audio_production_outer_callback_entered() noexcept {}
void audio_production_outer_callback_exited(bool) noexcept {}

bool selected_native_audio_name(const SelectionSnapshot& selection,std::uint64_t& name) noexcept {
    std::lock_guard lock(state_mutex);return native_name_locked(selection,name);
}
bool reserve_selection_audio_activation(const SelectionSnapshot& selection,const SelectionActivationListContext& context,
    SelectionActivationReservationDiagnostic* diagnostic) noexcept {
    std::lock_guard lock(state_mutex);
    SelectionActivationReservationDiagnostic facts;
    facts.selection_generation=selection.generation;
    auto finish=[&](SelectionActivationReservationFailure failure) {
        facts.failure=failure;facts.reservation_generation=machine.generation();facts.state=machine.state();
        if(diagnostic)*diagnostic=facts;
        return failure==SelectionActivationReservationFailure::None;
    };
    std::uint64_t name=0;
    if(!native_name_locked(selection,name))return finish(SelectionActivationReservationFailure::NativeSelection);
    if(!live_context(context))return finish(SelectionActivationReservationFailure::ListContext);
    if(active.token.valid())return finish(SelectionActivationReservationFailure::ActiveAdmission);
    const auto menu=capture_ready_menu_session();
    facts.menu_generation=menu.generation;
    if(!menu)return finish(SelectionActivationReservationFailure::MenuNotReady);
    if(menu.widget!=context.object || menu.widget_identity.internal_index!=context.internal_index
        || menu.widget_identity.serial_number!=context.serial_number)return finish(SelectionActivationReservationFailure::MenuContext);
    void* widget=nullptr;UObjectLiveHandle identity{};
    if(!native_controller_list_exact(menu) || !resolve_piano_menu_widget_binding(menu.list,widget,identity)
        || widget!=context.object || identity.internal_index!=context.internal_index
        || identity.serial_number!=context.serial_number)return finish(SelectionActivationReservationFailure::NativeListBinding);
    if(!menu_session_generation_exact(menu.generation,true))return finish(SelectionActivationReservationFailure::MenuGeneration);
    if(machine.state()==SelectionActivationReservationState::Reserved) {
        const bool same=exact(selection,reservation.selection) && context_exact(context,reservation.context)
            && menu.generation==reservation.menu.generation && menu.controller==reservation.menu.controller
            && menu.list==reservation.menu.list;
        return finish(same?SelectionActivationReservationFailure::None:SelectionActivationReservationFailure::ReservationDrift);
    }
    if(!machine.reserve())return finish(SelectionActivationReservationFailure::ReservationState);
    reservation={selection,context,menu};return finish(SelectionActivationReservationFailure::None);
}
const char* selection_activation_reservation_failure_name(SelectionActivationReservationFailure failure) noexcept {
    switch(failure) {
    case SelectionActivationReservationFailure::None:return "none";
    case SelectionActivationReservationFailure::NativeSelection:return "native_selection";
    case SelectionActivationReservationFailure::ListContextSnapshot:return "list_context_snapshot";
    case SelectionActivationReservationFailure::ListContext:return "list_context";
    case SelectionActivationReservationFailure::ActiveAdmission:return "active_admission";
    case SelectionActivationReservationFailure::MenuNotReady:return "menu_not_ready";
    case SelectionActivationReservationFailure::MenuContext:return "menu_context";
    case SelectionActivationReservationFailure::MenuGeneration:return "menu_generation";
    case SelectionActivationReservationFailure::NativeListBinding:return "native_list_binding";
    case SelectionActivationReservationFailure::ReservationDrift:return "reservation_drift";
    case SelectionActivationReservationFailure::ReservationState:return "reservation_state";
    }return "unknown";
}
SelectionActivationHandoff prepare_selection_audio_activation_handoff(const SelectionSnapshot& selection,
    const SelectionActivationListContext& context) noexcept {
    std::lock_guard lock(state_mutex);SelectionActivationHandoff out;
    if(machine.state()!=SelectionActivationReservationState::Reserved)return out;
    if(!context_exact(context,reservation.context) || !live_context(context)) {
        out.result=SelectionActivationHandoffResult::ContextDrift;return out;
    }
    if(!exact(selection,reservation.selection) || !registry().selection_matches(selection)) {
        out.result=SelectionActivationHandoffResult::PriorSelectionDrift;return out;
    }
    out={machine.generation(),selection.generation,reservation.confirmed
        ?SelectionActivationHandoffResult::AlreadyConfirmed:SelectionActivationHandoffResult::Prepared};return out;
}
SelectionActivationHandoffResult confirm_selection_audio_activation_handoff(const SelectionActivationHandoff& handoff,
    const SelectionSnapshot& selection,const SelectionActivationListContext& context) noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.state()!=SelectionActivationReservationState::Reserved)return SelectionActivationHandoffResult::NoPendingReservation;
    if(machine.generation()!=handoff.reservation_generation)return SelectionActivationHandoffResult::ReservationGenerationDrift;
    if(!context_exact(context,reservation.context) || !live_context(context))return SelectionActivationHandoffResult::ContextDrift;
    const auto& prior=reservation.selection;
    if(reservation.confirmed)return exact(selection,prior) && handoff.prior_selection_generation==prior.generation
        && registry().selection_matches(selection)?SelectionActivationHandoffResult::Confirmed:SelectionActivationHandoffResult::PriorSelectionDrift;
    if(handoff.prior_selection_generation!=prior.generation)return SelectionActivationHandoffResult::PriorSelectionDrift;
    if(prior.generation==UINT64_MAX)return SelectionActivationHandoffResult::GenerationWrap;
    if(selection.generation==prior.generation)return SelectionActivationHandoffResult::SameGeneration;
    if(selection.generation!=prior.generation+1)return SelectionActivationHandoffResult::SkippedGeneration;
    if(selection.storage!=prior.storage)return SelectionActivationHandoffResult::StorageDrift;
    if(selection.song!=prior.song)return SelectionActivationHandoffResult::SongDrift;
    if(selection.profile!=prior.profile || selection.profile_index!=prior.profile_index)return SelectionActivationHandoffResult::ProfileDrift;
    if(selection.visible_index!=prior.visible_index)return SelectionActivationHandoffResult::VisibleIndexDrift;
    if(selection.base_slot!=prior.base_slot)return SelectionActivationHandoffResult::BaseSlotDrift;
    if(!registry().selection_matches(selection))return SelectionActivationHandoffResult::PriorSelectionDrift;
    reservation.selection=selection;reservation.confirmed=true;return SelectionActivationHandoffResult::Confirmed;
}
SelectionActivationHandoffResult publish_selection_audio_activation_handoff(const SelectionActivationHandoff& handoff,
    const SelectionActivationListContext& context,int32_t index,int32_t slot,bool alias,SelectionSnapshot& published) noexcept {
    std::lock_guard lock(state_mutex);published={};
    if(machine.generation()!=handoff.reservation_generation)return SelectionActivationHandoffResult::ReservationGenerationDrift;
    if(!context_exact(context,reservation.context) || !live_context(context))return SelectionActivationHandoffResult::ContextDrift;
    if(reservation.confirmed) {
        if(!alias || index!=reservation.selection.visible_index || slot!=reservation.selection.base_slot)
            return SelectionActivationHandoffResult::ContextDrift;
        const auto result=confirm_selection_audio_activation_handoff(handoff,reservation.selection,context);
        if(result==SelectionActivationHandoffResult::Confirmed)published=reservation.selection;
        return result;
    }
    if(!registry().publish_activation_selection_handoff(reservation.selection,index,slot,handoff.reservation_generation,alias,published))
        return SelectionActivationHandoffResult::PriorSelectionDrift;
    return confirm_selection_audio_activation_handoff(handoff,published,context);
}
SelectionActivationNotificationResult observe_selection_audio_activation_index_notification(const SelectionSnapshot& selection,
    const SelectionActivationListContext& context,int32_t index,int32_t slot) noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.state()!=SelectionActivationReservationState::Reserved)return SelectionActivationNotificationResult::NoPendingReservation;
    if(!context_exact(context,reservation.context) || !live_context(context))return SelectionActivationNotificationResult::ContextDrift;
    if(reservation.notified)return SelectionActivationNotificationResult::DuplicateNotification;
    if(!reservation.confirmed || !exact(selection,reservation.selection)
        || !registry().preserve_confirmed_activation_index_notification(selection,index,slot,true))return SelectionActivationNotificationResult::SelectionDrift;
    reservation.notified=true;return SelectionActivationNotificationResult::ConfirmedSameSelection;
}
bool revoke_selection_audio_activation_if_pending() noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.state()!=SelectionActivationReservationState::Reserved
        || reservation.transfer!=SelectionActivationTransferState::Pending)return false;
    revoke_locked();return true;
}
void revoke_selection_audio_activation() noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.state()==SelectionActivationReservationState::Reserved
        && reservation.transfer==SelectionActivationTransferState::Pending)revoke_locked();
}
std::uint64_t selection_audio_activation_revocation_epoch() noexcept {std::lock_guard lock(state_mutex);return revocation_epoch;}
bool revoke_selection_audio_activation_for_menu_session(const SelectionSnapshot& selection,std::uint64_t generation) noexcept {
    std::lock_guard lock(state_mutex);
    if(machine.state()!=SelectionActivationReservationState::Reserved)return !active.token.valid();
    if(reservation.menu.generation!=generation || !exact(reservation.selection,selection))return false;
    revoke_locked();return true;
}
SelectionActivationTerminalResult terminate_selection_audio_activation_for_menu_session(const MenuSessionSnapshot& menu,
    const SelectionSnapshot& selection,SelectionActivationTerminalReason reason) noexcept {
    std::lock_guard lock(state_mutex);SelectionActivationTerminalResult result;
    result.reservation_state=machine.state();result.reservation_generation=machine.generation();
    result.transfer_before=result.transfer_after=reservation.transfer;result.handoff_confirmed=reservation.confirmed;
    if(machine.state()!=SelectionActivationReservationState::Reserved
        || reservation.transfer!=SelectionActivationTransferState::Pending)return result;
    result.identity_exact=menu && menu.generation==reservation.menu.generation && menu.controller==reservation.menu.controller
        && menu.list==reservation.menu.list && native_controller_list_exact(menu)
        && menu_session_generation_exact(menu.generation,false) && menu.widget==reservation.context.object
        && menu.widget_identity.internal_index==reservation.context.internal_index
        && menu.widget_identity.serial_number==reservation.context.serial_number
        && live_context(reservation.context)
        && exact(selection,reservation.selection) && registry().selection_matches(selection);
    const bool seam=reason==SelectionActivationTerminalReason::ListClose || reason==SelectionActivationTerminalReason::State5Exit;
    if(result.identity_exact && seam && reservation.confirmed) {
        result.first_failure=SelectionActivationTerminalFailure::None;
        reservation.transfer=SelectionActivationTransferState::Preserved;result.decision=SelectionActivationTerminalDecision::Preserved;
    }else {
        result.first_failure=!result.identity_exact?SelectionActivationTerminalFailure::ReservationContext
            :!seam?SelectionActivationTerminalFailure::PreservationSeam:SelectionActivationTerminalFailure::HandoffUnconfirmed;
        revoke_locked();result.decision=SelectionActivationTerminalDecision::Revoked;
        result.transfer_after=SelectionActivationTransferState::Retired;
    }
    if(result.decision==SelectionActivationTerminalDecision::Preserved)result.transfer_after=reservation.transfer;
    return result;
}
bool reclaim_selection_audio_activation_for_controller(void* controller,SelectionActivationTerminalReason) noexcept {
    std::lock_guard lock(state_mutex);
    if(reservation.menu.controller!=controller)return false;
    if(active.token.valid())return false;
    if(machine.state()==SelectionActivationReservationState::Consumed) {
        if(!thaw_locked(reservation.selection,machine.generation()))return false;
        (void)machine.retire_consumed(machine.generation());
    }
    revoke_locked();return true;
}

struct SelectionActivationClaim::Impl {
    SelectionSnapshot selection;
    std::uint64_t generation=0;
    void* wrapper=nullptr;
    bool armed=true;
    ~Impl(){if(armed)abandon_claim(selection,generation);}
};
SelectionActivationClaim::SelectionActivationClaim() noexcept=default;
SelectionActivationClaim::SelectionActivationClaim(SelectionActivationClaim&&) noexcept=default;
SelectionActivationClaim& SelectionActivationClaim::operator=(SelectionActivationClaim&&) noexcept=default;
SelectionActivationClaim::~SelectionActivationClaim() noexcept=default;
SelectionActivationClaim::operator bool() const noexcept {return impl_ && impl_->armed;}
SelectionSnapshot selection_audio_activation_claim_selection(const SelectionActivationClaim& claim) noexcept {
    return claim?claim.impl_->selection:SelectionSnapshot{};
}
SelectionActivationClaim claim_selection_audio_activation(void* wrapper,uintptr_t caller) noexcept {
    SelectionActivationClaim claim;
    try {
        std::lock_guard lock(state_mutex);std::uint64_t name=0;void* chart_wrapper=nullptr;
        if(!rva::PersistentChartExpandCaller || caller!=rva::PersistentChartExpandCaller || !wrapper
            || active_menu_session_generation()!=0 || machine.state()!=SelectionActivationReservationState::Reserved
            || reservation.transfer!=SelectionActivationTransferState::Preserved || !reservation.confirmed
            // Native lifetime is bounded by the confirmed session's terminal/
            // destructor reclamation; a native controller has no GU serial.
            || !native_controller_list_exact(reservation.menu)
            || !core::safe_read_field(reservation.menu.controller,runtime_layouts::PianoChartController::chart,chart_wrapper)
            || chart_wrapper!=wrapper || !native_name_locked(reservation.selection,name))return claim;
        auto state=std::make_unique<SelectionActivationClaim::Impl>();state->armed=false;
        if(!registry().freeze_active_profile_for_activation(reservation.selection,machine.generation(),state->selection))return claim;
        state->generation=machine.generation();state->wrapper=wrapper;
        if(!machine.consume(state->generation)) {
            (void)thaw_locked(state->selection,state->generation);return claim;
        }
        reservation.selection=state->selection;reservation.transfer=SelectionActivationTransferState::ClaimedFrozen;
        state->armed=true;claim.impl_=std::move(state);
    }catch(...){}
    return claim;
}

struct SelectionAudioAdmission::Impl {
    SelectionSnapshot selection;
    CustomContextToken token;
    std::uint64_t activation=0;
    void* wrapper=nullptr;
    bool committed=false,owns=false;
    ~Impl(){if(owns && !committed){std::lock_guard lock(state_mutex);
        if(active.token==token && !retire_active_locked())disabled=true;}}
};
SelectionAudioAdmission::SelectionAudioAdmission() noexcept=default;
SelectionAudioAdmission::SelectionAudioAdmission(SelectionAudioAdmission&&) noexcept=default;
SelectionAudioAdmission& SelectionAudioAdmission::operator=(SelectionAudioAdmission&&) noexcept=default;
SelectionAudioAdmission::~SelectionAudioAdmission() noexcept=default;
SelectionAudioAdmission::operator bool() const noexcept {return impl_ && impl_->owns;}
SelectionAudioAdmission begin_selection_audio_admission(SelectionActivationClaim claim,bool complete) noexcept {
    SelectionAudioAdmission admission;
    if(!claim || !complete)return admission;
    try {
        std::lock_guard lock(state_mutex);std::uint64_t name=0;
        if(active.token.valid() || !native_name_locked(claim.impl_->selection,name)
            || machine.generation()!=claim.impl_->generation || machine.state()!=SelectionActivationReservationState::Consumed
            || !exact(reservation.selection,claim.impl_->selection) || next_lease==UINT64_MAX)return admission;
        auto state=std::make_unique<SelectionAudioAdmission::Impl>();
        state->selection=claim.impl_->selection;state->activation=claim.impl_->generation;state->wrapper=claim.impl_->wrapper;
        // These tokens own chart/profile publication only; no Sound/controller
        // or bank pointer is fabricated as native playback authority.
        state->token={state->selection.generation,++next_lease,next_lease,
            ff7rp::pipeline::fnv1a64_string(state->selection.song->id)};
        if(!registry().acquire_selection_guard(state->selection,state->token))return admission;
        active={state->selection,state->token,state->activation,state->wrapper,false};
        reservation.transfer=SelectionActivationTransferState::AdmissionOwned;
        state->owns=true;claim.impl_->armed=false;admission.impl_=std::move(state);
    }catch(...){}
    return admission;
}
AudioRouteArmLease commit_selection_audio_admission(SelectionAudioAdmission& admission) noexcept {
    if(!admission)return {};
    std::lock_guard lock(state_mutex);auto& state=*admission.impl_;std::uint64_t name=0;
    if(state.committed || active.token!=state.token || !native_name_locked(state.selection,name)
        || !registry().selection_guard_matches(state.selection,state.token)
        || !registry().publish_playback_from_selection_guard(state.selection,state.token))return {};
    active.committed=true;state.committed=true;reservation.transfer=SelectionActivationTransferState::Retired;
    return {AudioRouteArmStatus::Armed,{state.token.lease_generation,state.token.song_key}};
}
AudioRouteLeaseIdentity selection_audio_admission_lease(const SelectionAudioAdmission& admission) noexcept {
    return admission?AudioRouteLeaseIdentity{admission.impl_->token.lease_generation,admission.impl_->token.song_key}:AudioRouteLeaseIdentity{};
}
bool cancel_selection_audio_admission(SelectionAudioAdmission& admission) noexcept {
    if(!admission)return true;
    {std::lock_guard lock(state_mutex);
     if(active.token!=admission.impl_->token || !retire_active_locked()){disabled=true;return false;}
     admission.impl_->owns=false;}
    admission={};return true;
}
bool capture_selection_audio_admission_authority(const SelectionAudioAdmission& admission,SelectionAudioAdmissionAuthority& out) noexcept {
    out={};if(!admission)return false;
    std::lock_guard lock(state_mutex);const auto& state=*admission.impl_;
    out.selection=state.selection;out.token=state.token;out.activation_generation=state.activation;
    out.route_lifecycle_epoch=state.token.route_generation;out.preparation_ordinal=state.token.lease_generation;
    out.exact=!state.committed && active.token==state.token && registry().selection_guard_matches(state.selection,state.token);
    return out.exact;
}
const char* selection_audio_authority_failure_name(SelectionAudioAuthorityFailure failure) noexcept {
    switch(failure) {
#define AUTHORITY_FAILURE(value, name) case SelectionAudioAuthorityFailure::value:return name
    AUTHORITY_FAILURE(None,"none"); AUTHORITY_FAILURE(NotExact,"not_exact");
    AUTHORITY_FAILURE(Disabled,"disabled"); AUTHORITY_FAILURE(ActiveToken,"active_token");
    AUTHORITY_FAILURE(ActiveSelection,"active_selection"); AUTHORITY_FAILURE(Activation,"activation");
    AUTHORITY_FAILURE(RouteEpoch,"route_epoch"); AUTHORITY_FAILURE(PreparationOrdinal,"preparation_ordinal");
    AUTHORITY_FAILURE(SelectionGuard,"selection_guard"); AUTHORITY_FAILURE(CurrentSelection,"current_selection");
    AUTHORITY_FAILURE(PlaybackSelection,"playback_selection"); AUTHORITY_FAILURE(PlaybackToken,"playback_token");
    AUTHORITY_FAILURE(PlaybackChartFailed,"playback_chart_failed");
#undef AUTHORITY_FAILURE
    }
    return "unknown";
}
bool selection_audio_admission_authority_matches(const SelectionAudioAdmissionAuthority& authority,
    SelectionAudioAuthorityDiagnostic* diagnostic) noexcept {
    std::lock_guard lock(state_mutex);
    SelectionAudioAuthorityDiagnostic facts;
    facts.committed=active.committed;
    facts.expected_selection=authority.selection.generation;facts.observed_selection=active.selection.generation;
    facts.expected_activation=authority.activation_generation;facts.observed_activation=active.activation;
    facts.expected_route_epoch=authority.route_lifecycle_epoch;facts.expected_preparation=authority.preparation_ordinal;
    facts.expected_token=authority.token;facts.observed_token=active.token;
    const auto finish=[&](SelectionAudioAuthorityFailure failure) {
        facts.failure=failure;if(diagnostic)*diagnostic=facts;
        return failure==SelectionAudioAuthorityFailure::None;
    };
    using Failure=SelectionAudioAuthorityFailure;
    if(!authority.exact)return finish(Failure::NotExact);
    if(disabled)return finish(Failure::Disabled);
    if(!authority.token.valid() || active.token!=authority.token)return finish(Failure::ActiveToken);
    if(!exact(active.selection,authority.selection))return finish(Failure::ActiveSelection);
    if(active.activation!=authority.activation_generation)return finish(Failure::Activation);
    if(authority.route_lifecycle_epoch!=active.token.route_generation)return finish(Failure::RouteEpoch);
    if(authority.preparation_ordinal!=active.token.lease_generation)return finish(Failure::PreparationOrdinal);
    if(!active.committed)return finish(registry().selection_guard_matches(authority.selection,authority.token)
        ?Failure::None:Failure::SelectionGuard);
    // Commit consumes the guard. Validate its real playback successor and the
    // current selection in one registry capture, never the denial-only cleanup.
    SelectionSnapshot observed;
    PlaybackSnapshot playback;
    Failure failure=Failure::None;
    const bool matched=registry().commit_if_selection_snapshot(authority.selection,observed,playback,[&] {
        if(!exact(playback,authority.selection))failure=Failure::PlaybackSelection;
        else if(playback.token!=authority.token)failure=Failure::PlaybackToken;
        else if(playback.chart_admission.state==ChartAdmissionState::Failed)failure=Failure::PlaybackChartFailed;
        return failure==Failure::None;
    });
    facts.observed_selection=observed.generation;facts.observed_token=playback.token;
    return finish(matched?Failure::None:(failure==Failure::None?Failure::CurrentSelection:failure));
}
bool capture_selection_audio_admission_diagnostic(const SelectionAudioAdmission& admission,ChartAudioDiagnosticPrewriteSnapshot& out) noexcept {
    out={};SelectionAudioAdmissionAuthority authority;
    if(!capture_selection_audio_admission_authority(admission,authority))return false;
    out.attempted=true;out.exact_admission=authority.exact;out.selection_generation=authority.selection.generation;
    out.route_generation=authority.token.route_generation;out.lease_generation=authority.token.lease_generation;out.song_key=authority.token.song_key;
    // Native startup state is intentionally not inferred from a chart lease.
    return true;
}
bool selection_audio_admission_cancellation_complete(const SelectionSnapshot& selection,const AudioRouteLeaseIdentity& lease) noexcept {
    std::lock_guard lock(state_mutex);
    return selection && lease.valid() && !(active.token.lease_generation==lease.generation && active.token.song_key==lease.song_key)
        && !registry().selection_guard_matches(selection,{selection.generation,lease.generation,lease.generation,lease.song_key});
}
AudioRouteArmLease arm_audio_route_for_song_id(const std::string& id) {
    std::lock_guard lock(state_mutex);
    if(disabled)return {AudioRouteArmStatus::Disabled,{}};
    if(!active.committed || !active.selection.song || active.selection.song->id!=id)return {AudioRouteArmStatus::NoRoute,{}};
    return {AudioRouteArmStatus::Armed,{active.token.lease_generation,active.token.song_key}};
}
void block_custom_audio_route_for_unresolved_chart_mutation() noexcept {
    {std::lock_guard lock(state_mutex);disabled=true;unresolved_mutation=true;}
    try {core::log(core::LogLevel::Error,"[native_audio_adapter] stage=chart_mutation outcome=terminal authority=retained native_release=none");}catch(...){}
}
void fail_required_chart_expansion(const SelectionAudioAdmissionAuthority& authority,void* wrapper) noexcept {
    {std::lock_guard lock(state_mutex);
     (void)registry().fail_chart_expansion(authority.selection,authority.token,wrapper);disabled=true;unresolved_mutation=true;}
    try {core::log(core::LogLevel::Error,"[native_audio_adapter] stage=required_expansion outcome=terminal selection="
        +std::to_string(authority.selection.generation)+" lease="+std::to_string(authority.token.lease_generation)
        +" authority=retained native_release=none");}catch(...){}
}
void fail_chord_voicing_chart(std::shared_ptr<const ChordVoicingBinding> binding) noexcept {
    std::lock_guard lock(state_mutex);if(registry().fail_chart_admission(binding))disabled=true;
}
AudioRouteCleanupResult release_audio_route_on_piano_list_return(int32_t) {
    std::lock_guard lock(state_mutex);
    const AudioRouteLeaseIdentity lease{active.token.lease_generation,active.token.song_key};
    if(machine.state()==SelectionActivationReservationState::Consumed && !active.token.valid())
        return {AudioRouteCleanupStatus::Retained,lease,false,false};
    if(!retire_active_locked())return {AudioRouteCleanupStatus::Retained,lease,false,false};
    if(machine.state()==SelectionActivationReservationState::Reserved)revoke_locked();
    // Relinquishes only mod chart/profile authority. Registered native resources
    // remain backend-retained; the game alone owns stop/pause/resume and voices.
    return {lease.valid()?AudioRouteCleanupStatus::Released:AudioRouteCleanupStatus::NoAction,lease,true,true};
}
bool clear_frozen_profile_if_audio_unowned() {
    std::lock_guard lock(state_mutex);
    if(active.token.valid() || machine.state()==SelectionActivationReservationState::Consumed)return false;
    registry().clear_frozen_profile();return true;
}
bool custom_audio_route_idle_for_menu_input() noexcept {
    std::lock_guard lock(state_mutex);
    return installed && !disabled && !active.token.valid() && machine.state()!=SelectionActivationReservationState::Consumed;
}
bool custom_audio_playback_clock(double& elapsed) noexcept {
    // Old synthetic tick accumulation was not a native playback clock. No
    // qualified native elapsed field is exposed; diagnostic consumers skip it.
    elapsed=0;return false;
}
bool install_audio_sead_hooks(const HookInstallContext&) {
    std::lock_guard lock(state_mutex);
    if(!native_audio::native_audio_layout(core::generated::kBuildId))return false;
    installed=true;audio_production_open_callback_admission();return true;
}
AudioRouteShutdownResult shutdown_audio_sead_with_result() {
    std::lock_guard lock(state_mutex);installed=false;
    if(machine.state()==SelectionActivationReservationState::Consumed && !active.token.valid())
        return {AudioRouteShutdownStatus::Retained,{AudioRouteCleanupStatus::Retained,{},false,false}};
    if(!retire_active_locked())return {AudioRouteShutdownStatus::Retained,{AudioRouteCleanupStatus::Retained,{},{},false}};
    revoke_locked();return {AudioRouteShutdownStatus::Succeeded,{}};
}
bool shutdown_audio_sead() {return shutdown_audio_sead_with_result().status==AudioRouteShutdownStatus::Succeeded;}
} // namespace ff7r::piano::game
