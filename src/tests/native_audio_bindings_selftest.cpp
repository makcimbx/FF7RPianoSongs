#include "game/native_audio_bindings.h"
#include "game/rvas.h"
#include "core/generated/build_identity.generated.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <vector>

using namespace ff7r::piano::game;
namespace na = native_audio;
namespace gu = uobject_locator_core;

struct Fixture {
    static constexpr bool older=std::string_view(ff7r::piano::core::generated::kBuildId)=="ff7rebirth-steam-win64-68fd6fde";
    static constexpr std::size_t bgm_resource=0x60;
    static constexpr std::size_t append_slot=older?0x280:0x288;
    static constexpr std::size_t backing_slot=older?0x2b0:0x2b8;
    static constexpr std::size_t interface_slot=older?0x2b8:0x2c0;
    static constexpr na::Address base = 0x10000000;
    std::map<na::Address, std::vector<unsigned char>> memory;
    na::Address next = 0x20000000, array = 0, chunks = 0, items = 0, root = 0, entries = 0, service = 0;
    std::array<na::BindingSeed, 3> seeds{};
    std::vector<std::string> texts;
    std::vector<na::NameWitness> names;
    std::shared_ptr<ff7rp::pipeline::NativeAudioArtifactSet> artifacts = std::make_shared<ff7rp::pipeline::NativeAudioArtifactSet>();
    int object_count = 0;
    na::Address drift = 0; unsigned drift_reads = 0;
    std::size_t reads = 0;
    std::size_t copied_bytes = 0;
    na::Address watched = 0;
    std::size_t watched_reads = 0;
    na::Address unreadable = 0;
    void (*after_read)(Fixture&, na::Address, std::size_t) noexcept = nullptr;
    na::Address alloc(std::size_t size) {
        const auto address = next; next += ((size + 15) / 16) * 16;
        memory[address].resize(size); return address;
    }
    template<class T> void put(na::Address address, T value) {
        auto it = memory.upper_bound(address); assert(it != memory.begin()); --it;
        assert(address - it->first + sizeof(T) <= it->second.size());
        std::memcpy(it->second.data() + address - it->first, &value, sizeof(value));
    }
    static bool read(void* context, na::Address address, void* out, std::size_t size) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        ++f.reads;
        if(address==f.watched)++f.watched_reads;
        if (address == f.unreadable) return false;
        auto it = f.memory.upper_bound(address); if (it == f.memory.begin()) return false; --it;
        if (address - it->first > it->second.size() || size > it->second.size() - (address - it->first)) return false;
        std::memcpy(out, it->second.data() + address - it->first, size);
        f.copied_bytes += size;
        if (address == f.drift && ++f.drift_reads == 2) static_cast<unsigned char*>(out)[0] ^= 1;
        if (f.after_read) f.after_read(f, address, size);
        return true;
    }
    std::uint64_t name(const std::string& text) {
        for (std::size_t i = 0; i < texts.size(); ++i) if (texts[i] == text) return i + 1;
        texts.push_back(text); return texts.size();
    }
    na::Address object(std::uint64_t name_value, na::Address cls, na::Address outer, int serial = 1, na::Address vtable = 0) {
        const auto object = alloc(128); const int index = object_count++;
        put(object, vtable); put(object + 12, index); put(object + 16, cls); put(object + 24, name_value); put(object + 32, outer);
        put(items + index * 24, object); put(items + index * 24 + 16, serial);
        return object;
    }
    int index(na::Address object) {
        int value = -1; assert(read(this, object + 12, &value, 4)); return value;
    }
    void zero_metadata() {
        for (const auto& s : seeds) for (const auto* w : {&s.candidate, &s.stock})
            for (unsigned i = 1; i < 7; ++i) put(items + index(w->objects[i]) * 24 + 16, 0);
    }
    na::ObjectWitness witness(na::ObjectNames n, na::Address vtable, bool candidate) {
        na::ObjectWitness w;
        auto& o = w.objects;
        o[2] = object(n.metaclass, 0, 0); put(o[2] + 16, o[2]);
        o[4] = object(n.package_class, o[2], 0);
        o[3] = object(n.script_package, o[4], 0);
        o[1] = object(n.class_name, o[2], o[3]);
        o[6] = o[4]; o[5] = object(n.package, o[6], 0);
        o[0] = object(n.instance, o[1], o[5], candidate ? 0 : 7, vtable);
        return w;
    }
    na::Address manager(unsigned role) const {
        return base + (role == 0 ? rva::BgmListRegistry : role == 1 ? rva::ScoreInfoRegistry : rva::PianoScoreRegistry);
    }
    gu::UObjectIdentityAccess access() {
        gu::UObjectIdentityAccess a{}; a.memory = {this, read}; a.array_address = array; return a;
    }
    void registered(unsigned role, int serial = 19) {
        const auto candidate = seeds[role].candidate.objects[0];
        put(items + index(candidate) * 24 + 16, serial);
        const auto payload = alloc(16);
        put(payload, index(candidate)); put(payload + 4, serial); put(payload + 8, seeds[role].association);
        put(manager(role) + 32, payload); put(manager(role) + 40, 1); put(manager(role) + 44, 1);
    }
    void fill_group(unsigned role, unsigned group, int count) {
        // Distinct live native baseline objects, not duplicate candidate/stock entries.
        if(memory.at(items).size()<4096*24) {
            const auto old=memory.at(items);items=alloc(4096*24);
            std::memcpy(memory.at(items).data(),old.data(),old.size());put(chunks,items);
            put(array+32,4096);put(array+36,4096);
        }
        const auto payload=alloc(static_cast<std::size_t>(count+1)*16);
        for(int n=0;n<count;++n) {
            const bool stock=group==0 && n==0;
            const auto object=stock?seeds[role].stock.objects[0]:this->object(0,0,0,7);
            put(payload+n*16,index(object));put(payload+n*16+4,7);
            put(payload+n*16+8,stock?seeds[role].stock_association:seeds[role].association);
        }
        put(manager(role)+group*16,payload);put(manager(role)+group*16+8,count);put(manager(role)+group*16+12,count);
    }
    Fixture() {
        array = alloc(48); chunks = alloc(8); items = alloc(128 * 24);
        put(array + 16, chunks); put(array + 32, 128); put(array + 36, 128); put(array + 40, 1); put(array + 44, 1); put(chunks, items);
        artifacts->build_id = ff7r::piano::core::generated::kBuildId;
        service = alloc(96); put(service + 88, base + rva::StreamableManagerVtable);
        memory[base + rva::StreamableServiceGlobal].resize(8); put(base + rva::StreamableServiceGlobal, service);
        constexpr const char* classes[]{"EndDataObjectBGMList", "EndDataObjectPianoScoreInfo", "EndDataObjectPianoScore"};
        constexpr const char* objects[]{"BGMList", "PianoScoreInfo", "PianoScore"};
        const na::Address vtables[]{rva::BgmListObjectVtable, rva::ScoreInfoObjectVtable, rva::PianoScoreObjectVtable};
        const na::Address appends[]{rva::BgmListAppendBridge,rva::ScoreInfoAppendBridge,rva::PianoScoreAppendBridge};
        memory[base+0x1000]={0x48,0x8b,0x41,0x58,0xc3};
        memory[base+0x1100]={0x48,0x8b,0x41,0x58,0x48,0x83,0xc0,0x58,0xc3};
        root = alloc(80); entries = alloc(3 * 24);
        memory[base + rva::DataObjectNameMap].resize(8); put(base + rva::DataObjectNameMap, root);
        put(root, entries); put(root + 8, 3); put(root + 12, 3); put(root + 56, 0); put(root + 72, 1);
        for (unsigned role = 0; role < 3; ++role) {
            memory[base+vtables[role]].resize(0x300);
            put(base+vtables[role]+append_slot,base+appends[role]);
            put(base+vtables[role]+backing_slot,base+0x1000);
            put(base+vtables[role]+interface_slot,base+0x1100);
            auto& s = seeds[role]; auto& p = artifacts->packages[role + 1];
            p.role = static_cast<ff7rp::pipeline::NativeAudioPackageRole>(role + 1);
            p.object_path = std::string("/Game/Custom/Fixture") + std::to_string(role) + "." + objects[role];
            p.class_path = std::string("/Script/EndDataObject.") + classes[role];
            s.candidate_names = {name(objects[role]), name(classes[role]), name("Class"), name("/Script/EndDataObject"), name("Package"), name(p.object_path.substr(0, p.object_path.find('.')))};
            s.stock_names = s.candidate_names; s.stock_names.package = name(std::string("/Game/DataObject/Resident/") + objects[role]);
            s.candidate = witness(s.candidate_names, base + vtables[role], true);
            s.stock = witness(s.stock_names, base + vtables[role], false);
            if (role) {
                for (const unsigned node : {2u, 3u, 4u, 6u}) s.stock.objects[node] = seeds[0].stock.objects[node];
                put(s.stock.objects[1] + 16, s.stock.objects[2]); put(s.stock.objects[1] + 32, s.stock.objects[3]);
                put(s.stock.objects[5] + 16, s.stock.objects[6]);
            }
            for (const unsigned node : {1u, 2u, 3u, 4u, 6u}) s.candidate.objects[node] = s.stock.objects[node];
            put(s.candidate.objects[0] + 16, s.stock.objects[1]); put(s.candidate.objects[5] + 16, s.stock.objects[6]);
            s.association = name("FixtureAssociation"); s.stock_association = name("Resident");
            s.owner.handle = alloc(160); s.owner.control = alloc(16); s.owner.requested_path = name(p.object_path);
            put(s.owner.handle + 152, service + 88);
            put(s.owner.handle + 16, static_cast<unsigned char>(1)); put(s.owner.control + 8, 1); put(s.owner.control + 12, 1);
            const auto path = alloc(24); put(path, s.owner.requested_path); put(s.owner.handle + 104, path); put(s.owner.handle + 112, 1); put(s.owner.handle + 116, 1);
            memory[manager(role)].resize(48);
            const auto group = alloc(16); put(group, index(s.stock.objects[0])); put(group + 4, 7); put(group + 8, s.stock_association);
            put(manager(role), group); put(manager(role) + 8, 1); put(manager(role) + 12, 1);
            put(entries + role * 24, s.stock_names.instance); put(entries + role * 24 + 8, manager(role)); put(entries + role * 24 + 16, role == 2 ? -1 : static_cast<int>(role + 1));
            if (role) {
                const auto backing_vtable = base + (role == 1 ? rva::ScoreInfoBackingVtable : rva::PianoScoreBackingVtable);
                const auto interface_vtable = base + (role == 1 ? rva::ScoreInfoInterfaceVtable : rva::PianoScoreInterfaceVtable);
                memory[interface_vtable].resize(48); put(interface_vtable + 40, base + (role == 1 ? rva::ScoreInfoTypedLookup : rva::PianoScoreTypedLookup));
                for (const auto* w : {&s.candidate, &s.stock}) {
                    const auto backing = alloc(96); put(backing, backing_vtable); put(backing + 88, interface_vtable);
                    put(w->objects[0] + 84, 1u); put(w->objects[0] + 88, backing);
                }
            }
        }
        for (std::size_t i = 0; i < texts.size(); ++i) names.push_back({texts[i], i + 1});
    }
    na::BindingResult capture(na::CohortBindings& b) {
        return b.capture(artifacts->build_id, base, access(), artifacts, seeds, {names.data(), names.size()});
    }
};

#ifndef FF7RP_NATIVE_AUDIO_BINDING_FIXTURE_ONLY
namespace {
bool track_capture_allocations=false;
std::size_t capture_allocations=0,capture_bytes=0;
std::size_t fail_capture_allocation=static_cast<std::size_t>(-1);
}
// Only the standalone binding test replaces the nothrow array allocation seam.
void* operator new[](std::size_t bytes,const std::nothrow_t&) noexcept {
    if(track_capture_allocations) {
        const auto index=capture_allocations++;capture_bytes+=bytes;
        if(index==fail_capture_allocation)return nullptr;
    }
    try{return ::operator new[](bytes);}catch(...){return nullptr;}
}
int main() {
    assert(na::native_audio_layout(ff7r::piano::core::generated::kBuildId));
    assert(!na::native_audio_layout("unknown"));
    assert(!na::native_audio_layout(Fixture::older?"ff7rebirth-steam-win64-6a16ced2":"ff7rebirth-steam-win64-68fd6fde"));
    using F = na::BindingFailure; using P = na::BindingPhase;
    static_assert(sizeof(na::BindingDiagnostic) <= 192);
    { Fixture f;na::CohortBindings b;
      f.reads=f.copied_bytes=0;
      track_capture_allocations=true;capture_allocations=capture_bytes=0;
      const auto result=f.capture(b);track_capture_allocations=false;assert(result);
      assert(capture_allocations==1 && capture_bytes==2048*96);
      std::cout<<"small capture reads="<<f.reads<<" reader_copied_bytes="<<f.copied_bytes
          <<" allocations="<<capture_allocations<<" bytes="<<capture_bytes<<'\n';
    }
    for(std::size_t allocation:{0u,1u,3u}) {
      Fixture f;na::CohortBindings b;
      if(allocation)for(unsigned role=0;role<3;++role)for(unsigned group=0;group<3;++group)f.fill_group(role,group,65);
      track_capture_allocations=true;capture_allocations=capture_bytes=0;fail_capture_allocation=allocation;
      const auto result=f.capture(b);track_capture_allocations=false;fail_capture_allocation=static_cast<std::size_t>(-1);
      assert(!result && !b.captured());
      assert(result.failure==F::Budget || result.failure==F::Read);
      assert(f.capture(b)); // No native mutation or terminal latch on an initial allocation failure.
    }
    { Fixture f;na::CohortBindings b;assert(f.capture(b));f.registered(0);
       track_capture_allocations=true;capture_allocations=capture_bytes=0;fail_capture_allocation=0;
       const auto result=b.latch_after_own_bridge(f.access(),0);
       track_capture_allocations=false;fail_capture_allocation=static_cast<std::size_t>(-1);
       assert(result.failure==F::Budget && b.terminal_failure().failure==F::Budget);
       const auto reads=f.reads;assert(b.validate(f.access(),P::BeforeBridge).failure==F::PartialMutation);
       assert(f.reads==reads);
    }
    for(unsigned operation=0;operation<3;++operation)for(unsigned boundary:{2u,3u})for(bool unreadable:{false,true}) {
       Fixture f;na::CohortBindings b;
       if(operation)assert(f.capture(b));
       if(operation==2)f.registered(0);
       f.watched=f.seeds[0].owner.handle+152;f.watched_reads=0;
       // Change only after the preceding read: record verification or final cohort verification.
       f.drift_reads=boundary-1;
       f.after_read=unreadable?+[](Fixture& f,na::Address a,std::size_t) noexcept {
           if(a==f.watched && f.watched_reads==f.drift_reads)f.unreadable=f.watched;
       }:+[](Fixture& f,na::Address a,std::size_t) noexcept {
           if(a==f.watched && f.watched_reads==f.drift_reads)f.put(f.watched,f.service+80);
       };
       const auto result=operation==0?f.capture(b):operation==1?b.validate(f.access(),P::BeforeBridge)
           :b.latch_after_own_bridge(f.access(),0);
       assert(result.failure==F::Drift && result.diagnostic.read_failed==unreadable);
       assert(std::string_view(result.diagnostic.stage)==(boundary==2?"record_reread":"cohort_reread"));
       assert(f.watched_reads==boundary && b.captured()==(operation!=0));
       if(operation==2) {
           const auto reads=f.reads;assert(b.terminal_failure().failure==F::Drift);
           assert(b.validate(f.access(),P::BeforeBridge).failure==F::PartialMutation && f.reads==reads);
       }
    }
    for(unsigned role=0;role<3;++role) {
      Fixture f;na::CohortBindings b;const na::Address tables[]{rva::BgmListObjectVtable,rva::ScoreInfoObjectVtable,rva::PianoScoreObjectVtable};
      f.put(f.base+tables[role]+Fixture::append_slot,na::Address{});
      assert(f.capture(b).failure==F::Relationship);
    }
    for(const auto slot:{Fixture::backing_slot,Fixture::interface_slot}) {
      Fixture f;na::CohortBindings b;f.put(f.base+rva::ScoreInfoObjectVtable+slot,na::Address{});
      assert(f.capture(b).failure==F::Backing);
    }
    {Fixture f;na::CohortBindings b;f.put(f.base+0x1000,static_cast<unsigned char>(0xcc));
     assert(f.capture(b).failure==F::Backing);}
    {Fixture f;na::CohortBindings b;f.put(f.seeds[1].candidate.objects[0]+85,static_cast<unsigned char>(0xff));
     assert(f.capture(b));} // Native dispatch reads the mode byte, not its padding.
    { Fixture f; f.zero_metadata(); na::CohortBindings b; assert(f.capture(b));
      assert(b.validate(f.access(), P::BeforeBridge));
      for (unsigned char role=0; role<3; ++role) {
          f.registered(role); assert(b.latch_after_own_bridge(f.access(), role));
      }
      assert(b.validate(f.access(), P::CurrentRegistered));
      // No zero metadata observation is promoted to a positive identity or lease.
      const auto object=f.seeds[2].candidate.objects[5];
      f.put(f.items+f.index(object)*24+16, 8);
      const auto result=b.validate(f.access(), P::CurrentRegistered);
      assert(result.failure==F::Identity && result.role==2);
      assert(result.diagnostic.node==5 && result.diagnostic.side==na::BindingSide::Candidate);
      assert(std::string_view(result.diagnostic.predicate)=="pinned_serial");
      assert(result.diagnostic.serial_observed && result.diagnostic.serial==8);
    }
    for (unsigned side=0; side<2; ++side) for (unsigned node=1; node<7; ++node) {
      // Package objects are side-specific; the other metadata nodes are shared.
      for (unsigned defect=0; defect<6; ++defect) {
        Fixture f; f.zero_metadata(); const auto& w=side?f.seeds[0].stock:f.seeds[0].candidate;
        const auto object=w.objects[node]; const auto index=f.index(object);
        if(defect==0) f.put(f.items+index*24+16,-3);
        if(defect==1) f.put(f.items+index*24+8,0x30000000u);
        if(defect==2) f.put(object+8,0x18000u);
        if(defect==3) f.put(f.items+index*24,object+8);
        if(defect==4) f.put(object+8,0x10u);
        if(defect==5) f.put(object+12,-1);
        na::CohortBindings b;const auto result=f.capture(b);assert(!result && !b.captured());
        assert(std::string_view(result.diagnostic.stage)=="identity");
        assert(result.diagnostic.node<7 && result.diagnostic.side!=na::BindingSide::None);
        if(defect==0) assert(result.diagnostic.serial_observed && result.diagnostic.serial==-3);
      }
    }
    for (unsigned side=0; side<2; ++side) for (unsigned node=1; node<7; ++node) {
      Fixture f; f.zero_metadata(); na::CohortBindings b; assert(f.capture(b));
      const auto object=(side?f.seeds[0].stock:f.seeds[0].candidate).objects[node];
      // Strict pinned zero cannot turn positive, even across our own bridge.
      f.registered(0); f.put(f.items+f.index(object)*24+16,9);
      const auto result=b.latch_after_own_bridge(f.access(),0); assert(result.failure==F::Identity);
      assert(std::string_view(result.diagnostic.predicate)=="pinned_serial");
      const auto reads=f.reads;const auto retained=b.terminal_failure();assert(f.reads==reads);
      assert(retained.diagnostic.node==result.diagnostic.node);
      assert(b.validate(f.access(),P::BeforeBridge).diagnostic.predicate==retained.diagnostic.predicate);
      assert(f.reads==reads);
    }
    for(unsigned role=0;role<3;++role) {
      Fixture f;f.zero_metadata();const auto stock=f.seeds[role].stock.objects[0];
      f.put(f.items+f.index(stock)*24+16,0);na::CohortBindings b;const auto result=f.capture(b);
      assert(result.failure==F::Identity && result.role==role);
      assert(result.diagnostic.side==na::BindingSide::Stock && result.diagnostic.node==0);
      assert(result.diagnostic.serial_observed && result.diagnostic.serial==0 && !result.diagnostic.zero_attempted);
    }
    for(unsigned node=1;node<7;++node) for(int serial:{0,2}) {
      Fixture f;na::CohortBindings b;assert(f.capture(b));
      const auto object=f.seeds[0].candidate.objects[node];f.put(f.items+f.index(object)*24+16,serial);
      const auto result=b.validate(f.access(),P::BeforeBridge);
      assert(result.failure==F::Identity && std::string_view(result.diagnostic.predicate)=="pinned_serial");
      assert(result.diagnostic.serial_observed && result.diagnostic.serial==serial);
    }
    for(int serial:{0,-1}) {
      Fixture f;f.zero_metadata();na::CohortBindings b;assert(f.capture(b));f.registered(0,serial);
      const auto result=b.latch_after_own_bridge(f.access(),0);
      assert(result.failure==F::Identity && result.diagnostic.node==0);
      assert(std::string_view(result.diagnostic.predicate)=="positive_instance");
      assert(!result.diagnostic.zero_attempted && result.diagnostic.serial==serial);
      assert(result.diagnostic.phase==P::AfterOwnBridge);
    }
    { Fixture f;f.zero_metadata();na::CohortBindings b;assert(f.capture(b));f.registered(0);
      na::Address payload=0;assert(Fixture::read(&f,f.manager(0)+32,&payload,8));f.put(payload+4,20);
      const auto result=b.latch_after_own_bridge(f.access(),0);assert(result.failure==F::RegistryWeak);
      assert(std::string_view(result.diagnostic.stage)=="registry");
    }
    { Fixture f; f.zero_metadata(); f.unreadable=f.seeds[1].candidate.objects[0];
      na::CohortBindings b;const auto result=f.capture(b);assert(result.failure==F::Identity);
      assert(std::string_view(result.diagnostic.stage)=="uniqueness");
      assert(std::string_view(result.diagnostic.predicate)=="object_header_read" && result.diagnostic.read_failed);
      assert(result.diagnostic.before_observed && !result.diagnostic.after_observed);
      const auto reads=f.reads; auto copy=result;copy.diagnostic.scanned=0;
      assert(result.diagnostic.scanned>0 && f.reads==reads);
    }
    { Fixture f;f.zero_metadata(); f.after_read=[](Fixture& f,na::Address a,std::size_t) noexcept {
          if(a==f.items+127*24+16)f.put(f.array+32,129);
      };
      na::CohortBindings b;const auto result=f.capture(b);assert(result.failure==F::Identity);
      assert(std::string_view(result.diagnostic.predicate)=="gu_header_drift");
      assert(result.diagnostic.before_observed && result.diagnostic.after_observed);
      assert(result.diagnostic.before.max_elements==128 && result.diagnostic.after.max_elements==129);
      assert((result.diagnostic.matches==std::array<unsigned,3>{1,1,1}) && result.diagnostic.scanned==128);
    }
    for(bool duplicate_tail:{false,true}) {
      Fixture f;f.zero_metadata();f.put(f.array+36,127);
      if(duplicate_tail)f.put(f.items+127*24,f.seeds[0].candidate.objects[0]);
      f.after_read=[](Fixture& f,na::Address a,std::size_t size) noexcept {
          if(a==f.seeds[0].candidate.objects[0] && size==40) {
              f.put(f.array+36,128);f.after_read=nullptr;
          }
      };
      na::CohortBindings b;const auto pending=f.capture(b);
      assert(!pending && pending.failure==F::IncompleteSnapshot && !b.captured() && b.terminal_failure());
      assert(std::string_view(pending.diagnostic.predicate)=="gu_header_drift");
      assert(pending.diagnostic.before.num_elements==127 && pending.diagnostic.after.num_elements==128);
      assert(pending.diagnostic.scanned==127 && !pending.diagnostic.read_failed);
      const auto retried=f.capture(b);
      if(duplicate_tail) {
          assert(!retried && retried.failure==F::Identity && !b.captured());
          assert(std::string_view(retried.diagnostic.predicate)=="candidate_ambiguous");
      } else assert(retried && b.captured() && b.validate(f.access(),P::BeforeBridge));
    }
    for(unsigned defect=0;defect<4;++defect) {
      Fixture f;f.zero_metadata();
      if(defect==0)f.unreadable=f.array+32;
      if(defect==1)f.unreadable=f.items+f.index(f.seeds[0].candidate.objects[0])*24+16;
      if(defect==2)f.after_read=[](Fixture& f,na::Address a,std::size_t) noexcept {
          if(a==f.items+127*24+16)f.unreadable=f.array+36;
      };
      if(defect==3)f.put(f.seeds[1].candidate.objects[0]+24,std::uint64_t{0});
      na::CohortBindings b;const auto result=f.capture(b);assert(result.failure==F::Identity);
      const char* predicates[]={"initial_gu_header","item_read","final_gu_header_read","candidate_missing"};
      assert(std::string_view(result.diagnostic.predicate)==predicates[defect]);
      assert(result.diagnostic.read_failed==(defect!=3));
      if(defect==3)assert(result.role==1 && result.diagnostic.matches[1]==0 && result.diagnostic.after_observed);
      const auto reads=f.reads;auto copy=result;copy.diagnostic.predicate="changed";
      assert(std::string_view(result.diagnostic.predicate)==predicates[defect] && f.reads==reads);
    }
    { Fixture f;f.zero_metadata();const auto object=f.seeds[0].candidate.objects[1];f.drift=object;
      na::CohortBindings b;const auto result=f.capture(b);assert(result.failure==F::Drift);
      assert(std::string_view(result.diagnostic.stage)=="record_reread");
      assert(result.diagnostic.node==1 && result.diagnostic.side==na::BindingSide::Candidate);
      assert(result.diagnostic.serial_observed && result.diagnostic.serial==0);
    }
    for(const int count:{65,static_cast<int>(na::kNativeAudioEntryLimit)}) {
      Fixture f;
      for(unsigned role=0;role<3;++role)for(unsigned group=0;group<3;++group)f.fill_group(role,group,count);
      na::Address payload=0;assert(Fixture::read(&f,f.manager(2)+32,&payload,8));
       f.watched=payload+(count-1)*16;
       f.reads=f.copied_bytes=0;
      track_capture_allocations=true;capture_allocations=capture_bytes=0;
      na::CohortBindings b;const auto result=f.capture(b);
      track_capture_allocations=false;
      if(!result)std::cerr<<na::binding_failure_name(result.failure)<<" role "<<int(result.role)<<'\n';
       assert(result && f.watched_reads==3);
       std::cout<<"group_count="<<count<<" capture reads="<<f.reads<<" reader_copied_bytes="<<f.copied_bytes
           <<" allocations="<<capture_allocations<<" bytes="<<capture_bytes<<'\n';
      if(count==static_cast<int>(na::kNativeAudioEntryLimit)) {
           assert(capture_allocations>4); // The single journal grows across record ranges.
          track_capture_allocations=true;capture_allocations=capture_bytes=0;fail_capture_allocation=2;
          const auto exhausted=b.validate(f.access(),P::BeforeBridge);
          track_capture_allocations=false;fail_capture_allocation=static_cast<std::size_t>(-1);
          assert(exhausted.failure==F::Budget || exhausted.failure==F::Read);
      }
       f.watched_reads=0;const auto reads=f.reads,bytes=f.copied_bytes;
       assert(b.validate(f.access(),P::BeforeBridge));assert(f.watched_reads==3);
       std::cout<<"group_count="<<count<<" validate reads="<<f.reads-reads
           <<" reader_copied_bytes="<<f.copied_bytes-bytes<<'\n';
      f.drift=f.watched;f.drift_reads=0;
      const auto drifting=b.validate(f.access(),P::BeforeBridge);assert(drifting.failure==F::Drift);
      assert(std::string_view(drifting.diagnostic.stage)=="record_reread");f.drift=0;
      // Even the last bounded entry is authenticated, never silently truncated.
      f.put(payload+(count-1)*16+4,8);assert(b.validate(f.access(),P::BeforeBridge).failure==F::RegistryWeak);
    }
    for(unsigned role=0;role<3;++role)for(unsigned group=0;group<3;++group) {
      Fixture f;f.fill_group(role,group,static_cast<int>(na::kNativeAudioEntryLimit)+1);
      na::CohortBindings b;assert(f.capture(b).failure==F::RegistryBounds);assert(!b.captured());
    }
    { Fixture f; na::CohortBindings b; const auto result = f.capture(b); if (!result) std::cerr << na::binding_failure_name(result.failure) << " role " << int(result.role) << '\n'; assert(result);
      assert(b.validate(f.access(), P::BeforeBridge));
      assert(!b.validate(f.access(), P::CurrentRegistered));
      for (unsigned char role = 0; role < 3; ++role) { f.registered(role); assert(b.latch_after_own_bridge(f.access(), role)); }
      assert(b.validate(f.access(), P::CurrentRegistered));
      // Ordinary map growth is allowed postbatch, strict native precalls reject it.
      f.put(f.root + 12, 4); assert(b.validate(f.access(), P::CurrentRegistered));
      assert(b.validate(f.access(), P::BeforeBridge).failure == F::HistoricalMapDrift);
      f.put(f.items + f.index(f.seeds[1].candidate.objects[0]) * 24 + 16, 20);
      assert(b.validate(f.access(), P::CurrentRegistered).failure == F::Identity);
    }
    { Fixture f; na::CohortBindings b; f.put(f.entries + 8, f.manager(1)); assert(f.capture(b).failure == F::MapBinding); assert(!b.captured()); }
    { Fixture f; na::CohortBindings b; f.put(f.entries + 16, 0); assert(f.capture(b).failure == F::MapChain); }
    { Fixture f; na::CohortBindings b; f.put(f.seeds[1].candidate.objects[0] + 84, 2u); assert(f.capture(b).failure == F::Backing); }
    { Fixture f; na::CohortBindings b; f.put(f.seeds[0].owner.handle + 16, static_cast<unsigned char>(0)); assert(f.capture(b).failure == F::Owner); }
    { Fixture f; na::CohortBindings b; f.registered(0); assert(f.capture(b).failure == F::RegistryMembership); }
    { Fixture f; na::CohortBindings b; f.drift = f.root; assert(f.capture(b).failure == F::Drift); }
    { Fixture f; na::CohortBindings b;
      f.artifacts->build_id = Fixture::older?"ff7rebirth-steam-win64-6a16ced2":"ff7rebirth-steam-win64-68fd6fde";
      assert(f.capture(b).failure == F::UnsupportedBuild); }
    { Fixture f; na::CohortBindings b; f.seeds[0].owner = f.seeds[1].owner; assert(!f.capture(b)); }
    { Fixture f; na::CohortBindings b; assert(f.capture(b)); f.registered(0);
      f.put(f.manager(0) + 40, 0); // Bridge changed identity but no authenticated publication.
      assert(b.latch_after_own_bridge(f.access(), 0).failure == F::RegistryMembership);
      assert(b.terminal_failure().failure == F::RegistryMembership);
      f.registered(0); assert(b.latch_after_own_bridge(f.access(), 0).failure == F::PartialMutation);
      assert(b.validate(f.access(), P::BeforeBridge).failure == F::PartialMutation);
    }
    { Fixture f; na::CohortBindings b; assert(f.capture(b));
      // Positive serial appearing without an own-bridge boundary is never adopted.
      f.put(f.items + f.index(f.seeds[0].candidate.objects[0]) * 24 + 16, 19);
      assert(b.validate(f.access(), P::BeforeBridge).failure == F::Identity);
    }
    { Fixture f; na::CohortBindings b; f.put(f.seeds[0].candidate.objects[0] + 24, f.seeds[0].candidate_names.instance | (1ull << 32)); assert(f.capture(b).failure == F::Identity); }
    { Fixture f; na::CohortBindings b; f.artifacts->packages[1].class_path = "/Script/EndDataObject.EndDataObjectPianoScore"; assert(f.capture(b).failure == F::Input); }
    { Fixture f; na::CohortBindings b; const auto& s = f.seeds[0];
      f.object(s.candidate_names.instance, s.candidate.objects[1], s.candidate.objects[5], 0, Fixture::base + rva::BgmListObjectVtable);
      const auto result=f.capture(b);assert(result.failure == F::Identity && result.role==0);
      assert(std::string_view(result.diagnostic.predicate)=="candidate_ambiguous");
      assert(result.diagnostic.matches[0]==2);
    }
    { Fixture f; na::CohortBindings b; const auto& s = f.seeds[0];
      const auto duplicate_package = f.object(s.candidate_names.package, s.candidate.objects[6], 0);
      f.object(s.candidate_names.instance, s.candidate.objects[1], duplicate_package, 0, Fixture::base + rva::BgmListObjectVtable);
      assert(f.capture(b).failure == F::Identity);
    }
    { Fixture f; na::CohortBindings b; assert(f.capture(b));
      // Independent acquisitions are checked together even for a single bridge.
      f.registered(0); f.put(f.seeds[2].owner.handle + 17, static_cast<unsigned char>(1));
      assert(b.latch_after_own_bridge(f.access(), 0).failure == F::Owner);
      assert(b.terminal_failure().role == 2);
    }
    { Fixture f; na::CohortBindings b; assert(f.capture(b));
      f.put(f.seeds[0].owner.handle + 152, f.service + 80);
      assert(b.validate(f.access(), P::BeforeBridge).failure == F::Owner);
    }
    { Fixture f; na::CohortBindings b; assert(f.capture(b)); for (unsigned char r = 0; r < 3; ++r) { f.registered(r); assert(b.latch_after_own_bridge(f.access(), r)); }
      const auto replacement = f.alloc(80); f.put(Fixture::base + rva::DataObjectNameMap, replacement);
      assert(!b.validate(f.access(), P::CurrentRegistered));
    }
    std::cout << "native_audio_bindings_selftest passed\n";
}
#endif
