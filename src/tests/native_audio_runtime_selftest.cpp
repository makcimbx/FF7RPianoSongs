// Exercise the production callbacks and readers with native-call doubles, not a
// parallel state machine. No game image/function or process is opened/executed.
#define FF7RP_NATIVE_AUDIO_BINDING_FIXTURE_ONLY
#include "native_audio_bindings_selftest.cpp"
#define FF7RP_NATIVE_AUDIO_RUNTIME_SELFTEST
#include "game/native_audio_runtime.cpp"
#include "pipeline/native_asset_hash.h"

#include <fstream>
#include <functional>
#include <future>
#include <latch>
#include <thread>

namespace {
struct RuntimeFixture;
RuntimeFixture* current=nullptr;
std::latch* invalidation_seen=nullptr;
struct RuntimeFixture : Fixture {
    na::Address owner=0, dispatcher=0, implementation=0, closure=0, package_nodes=0;
    std::array<na::Address,3> stock_rows{},new_rows{};
    std::shared_ptr<na::PreparedNativeAudio> work;
    std::filesystem::path directory;
    std::uint64_t stock_score=0,stock_bgm=0;
    unsigned mounts=0,acquires=0,bridges=0,reserves=0,updaters=0,initializers=0,name_calls=0,stock_resets=0,stock_removes=0;
    int incomplete_bridge=-1;
    bool immediate=false,engine_locked=false,update_in_lookup=false;
    std::atomic_bool shutdown_called{false};
    std::latch* mount_entered=nullptr;
    std::latch* mount_release=nullptr;
    std::latch* identity_entered=nullptr;
    std::latch* identity_release=nullptr;
    std::latch* entry_entered=nullptr;
    std::latch* entry_release=nullptr;
    unsigned entry_calls=0;
    bool reenter_entry=false,stop_inside_entry=false;
    unsigned disabled=0,removed=0,installed=0;
    int fail_install=-1,entry_during_install=-1,activity_during_install=-1;
    bool stop_inside_mount=false;
    bool original_before_barrier=false;
    unsigned lookups=0;
    unsigned notifications=0;
    bool initializer_returned=false,factory_active=false,duplicate_initialize=false;
    bool break_after_initialize=false,stop_after_initialize=false,throw_initialize=false,throw_factory=false;
    bool stop_in_notification=false,reenter_notification=false;
    std::latch* notification_entered=nullptr;
    std::latch* notification_release=nullptr;
    std::vector<std::pair<na::Address,std::vector<unsigned char>>> warm_headers;

    explicit RuntimeFixture(std::string song_id="RuntimeFixture") {
        current=this;
        na::g_base=base;na::g_lookup_dispatch=false;na::test_lookup_call=nullptr;na::g_failure.store(na::RuntimeFailure::None);
        na::g_origin.store(true);na::g_entry_seen.store(false);na::g_hooks_ready.store(true);na::g_installed=0;
        na::g_entry_forwarding.store(0);na::g_entry_forwarding_uncertain.store(false);na::test_entry_handoff=nullptr;
        na::g_failure_reported.store(false);na::g_capture_reported.store(false);
        na::g_accepting.store(true);na::g_captured.store(false);na::g_initializer_seen.store(false);
        na::g_initializer_notified.store(false);
        na::g_stock_admitted.store(false);
        na::g_updater_thread.store(0);na::g_stock_selection.store(0);na::g_working.clear();
        na::g_count=na::g_next=0;na::g_queue.fill({});na::g_mount={};
        na::g_callbacks.open();na::test_memory={this,Fixture::read};
        na::test_signature=[](std::string_view) {return true;};
        na::test_updater_caller=base+rva::NativeAudioUpdaterReturn;
        na::g_services={identity,score};
        na::original_factory=factory;na::original_shutdown=noop;na::original_destructor=receiver_noop;
        na::original_entry=exe_entry;
        na::original_initialize=initialize;na::original_updater=updater;
        na::original_append=append;na::original_remove=append;na::original_reset=receiver_noop;
        na::original_reserve=reserve;na::original_lookup=lookup;
        na::test_entry=entry;
        owner=alloc(1024);dispatcher=alloc(16);implementation=alloc(1456);closure=alloc(40);
        put(owner,base+rva::PackageStoreOwnerVtable);put(owner+8,base+rva::PackageStoreOwnerInterfaceVtable);
        put(owner+648,dispatcher);put(dispatcher+8,implementation);
        memory[base+rva::PackageStoreOwnerGlobal].resize(8);
        memory[base+rva::PackageStoreGeneration].resize(4);
        memory[base+rva::IoDispatcherGlobal].resize(8);put(base+rva::IoDispatcherGlobal,dispatcher);
        const auto event=alloc(16);put(implementation+1432,event);put(implementation+1440,1);put(implementation+1444,1);
        put(event,closure);put(event+8,1);put(closure,base+rva::PackageStoreSubscriptionVtable);
        put(closure+16,std::uint64_t{77});put(closure+24,owner+680);put(closure+32,base+rva::PackageStoreMountCallback);
        package_nodes=alloc(5*24);const auto map=owner+680+152;
        put(map,package_nodes);put(map+8,1);put(map+12,5);put(map+56,0);put(map+72,1);
        put(package_nodes,na::kStockPackageWitness);put(package_nodes+8,alloc(32));put(package_nodes+16,-1);
        artifacts->identity=std::move(song_id);artifacts->container_id=11;
        artifacts->music_name="FixtureMusic";artifacts->bgm_key="FixtureBgm";artifacts->score_name="FixtureScore";
        auto& music=artifacts->packages[0];music.role=ff7rp::pipeline::NativeAudioPackageRole::Music;
        music.object_path="/Game/Custom/Music.FixtureMusic";music.class_path="/Script/Fixture.Music";
        for(std::size_t i=0;i<4;++i) {
            artifacts->packages[i].package_id=100+i;artifacts->packages[i].public_export_hash=200+i;
            const auto node=package_nodes+(i+1)*24;
            put(node,artifacts->packages[i].package_id);put(node+8,alloc(32));put(node+16,i==3?-1:static_cast<int>(i+2));
        }
        stock_score=name("OrdinaryStockScore");stock_bgm=name("OrdinaryStockBgm");
        artifacts->base_score_name="OrdinaryStockScore";
        for(std::size_t i=0;i<3;++i){stock_rows[i]=alloc(128);new_rows[i]=alloc(128);}
        put(stock_rows[0]+0x50,std::uint64_t{0xbad000001}); // Alternate BGM is not Music.
        put(stock_rows[0]+bgm_resource,name("OrdinaryStockMusic"));put(stock_rows[1]+80,stock_bgm);
        wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
        directory=std::filesystem::path(temp)/(L"ff7rp-native-runtime-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);artifacts->container_prefix=directory/L"cohort";
        constexpr const char* extensions[]={".pak",".utoc",".ucas"}; // Order is intentionally not prescribed.
        for(std::size_t i=0;i<3;++i) {
            auto& file=artifacts->files[i];file.path=artifacts->container_prefix;file.path+=extensions[i];
            {std::ofstream out(file.path,std::ios::binary);out<<"fixture";}
            const std::string_view text="fixture";
            const auto digest=ff7rp::pipeline::native_sha256({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
            file.bytes=7;file.content_hash=0;
            for(unsigned j=0;j<8;++j)file.content_hash|=static_cast<std::uint64_t>(digest[j])<<(j*8);
        }
        work=na::prepare_native_audio(artifacts,41);assert(work);assert(na::enqueue_native_audio(work));
    }
    ~RuntimeFixture() {
        assert(na::shutdown_native_audio_runtime());na::g_queue.fill({});work.reset();
        na::test_memory={};na::test_entry=nullptr;na::test_signature=nullptr;na::test_lookup_call=nullptr;na::g_lookup_dispatch=false;
        na::test_install_hook=nullptr;na::test_disable_hook=nullptr;na::test_remove_hook=nullptr;
        na::test_entry_handoff=nullptr;
        std::filesystem::remove_all(directory);current=nullptr;
    }
    static bool identity(gu::UObjectIdentityAccess& out) noexcept {
        if(current->identity_entered){current->identity_entered->count_down();current->identity_release->wait();}
        out=current->access();return true;
    }
    static void noop() {current->shutdown_called.store(true);}
    static int exe_entry() {
        auto& f=*current;++f.entry_calls;
        assert(na::g_callbacks.callbacks_in_flight()==0);
        assert(na::g_entry_forwarding.load()>0);
        if(f.stop_inside_entry){assert(!na::shutdown_native_audio_runtime());assert(!f.disabled && !f.removed);}
        if(f.reenter_entry && f.entry_calls==1)assert(na::ordinary_entry_hook()==17);
        if(f.entry_entered){f.entry_entered->count_down();f.entry_release->wait();}
        return 17;
    }
    static void receiver_noop(void*) {}
    static void initialize(void* owner) {
        assert(owner==reinterpret_cast<void*>(current->owner));++current->initializers;
        if(current->throw_initialize)throw std::runtime_error("mock initializer failure");
        current->initializer_returned=true;
    }
    static void initialized() noexcept {
        auto& f=*current;++f.notifications;
        assert(f.initializer_returned && !f.factory_active && na::g_captured.load() && na::provenance());
        assert(na::g_initializer_notified.load() && !na::g_invalidator_depth && !na::g_work_depth
            && !na::NativeEntryGate::inside_entry() && na::g_callbacks.callbacks_in_flight()>0);
        {std::unique_lock lock(na::g_lifetime,std::try_to_lock);assert(lock.owns_lock());}
        {na::NativeEntryGate::Writer gate(na::g_managers);assert(gate);}
        assert(na::g_queue_mutex.try_lock());na::g_queue_mutex.unlock();
        assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls);
        if(f.notification_entered){f.notification_entered->count_down();f.notification_release->wait();}
        if(f.reenter_notification)na::initialize_hook(reinterpret_cast<void*>(f.owner));
        if(f.stop_in_notification){assert(!na::shutdown_native_audio_runtime());assert(!f.disabled && !f.removed);}
    }
    static void factory() {
        auto& f=*current;const auto notifications=f.notifications;f.factory_active=true;
        f.put(base+rva::PackageStoreOwnerGlobal,f.owner+8);f.put(base+rva::PackageStoreGeneration,1u);
        na::initialize_hook(reinterpret_cast<void*>(f.owner));
        assert(f.notifications==notifications);
        if(f.duplicate_initialize)na::initialize_hook(reinterpret_cast<void*>(f.owner));
        if(f.break_after_initialize)f.put(base+rva::PackageStoreGeneration,2u);
        if(f.stop_after_initialize)assert(!na::shutdown_native_audio_runtime());
        f.factory_active=false;
        if(f.throw_factory)throw std::runtime_error("mock factory failure");
    }
    static void updater(void*) {current->engine_locked=true;++current->updaters;current->engine_locked=false;}
    static void* lookup(void*,std::uint64_t,std::uint64_t*,unsigned char*) {
        ++current->lookups;
        if(current->update_in_lookup)current->frame();return nullptr;
    }
    static void name_ctor(std::uint64_t* out,const wchar_t* text,std::int32_t mode) {
        ++current->name_calls;
        assert(!current->engine_locked);std::wstring wide(text);std::string narrow;
        for(const auto c:wide){assert(c>=32 && c<=126);narrow.push_back(static_cast<char>(c));}
        if(!mode && std::find(current->texts.begin(),current->texts.end(),narrow)==current->texts.end()){*out=0;return;}
        *out=current->name(narrow);
    }
    static void* mount(void*,void* status,void*,void*,void*) {
        auto& f=*current;assert(!f.engine_locked);++f.mounts;
        assert(na::g_stock_admitted.load() && !na::NativeEntryGate::inside_entry());
        assert(na::g_queue_mutex.try_lock());na::g_queue_mutex.unlock();
        if(f.stop_inside_mount){assert(!na::shutdown_native_audio_runtime());assert(!f.disabled && !f.removed);}
        if(f.mount_entered){f.mount_entered->count_down();f.mount_release->wait();}
        f.put(f.owner+680+152+8,5);f.put(f.package_nodes+16,1);
        *static_cast<std::uint32_t*>(status)=0;return status;
    }
    static void* acquire(void* manager,void* output,void* path,void*,std::int32_t priority,void* zero1,void* zero2,void*) {
        auto& f=*current;assert(!f.engine_locked && manager==reinterpret_cast<void*>(f.service+88));
        assert(!priority && !zero1 && !zero2);++f.acquires;
        const auto requested=static_cast<na::SoftPath*>(path)->name;
        for(auto& seed:f.seeds)if(seed.owner.requested_path==requested) {
            f.put(seed.owner.handle+16,static_cast<unsigned char>(f.immediate));
            auto* pair=static_cast<na::Address*>(output);pair[0]=seed.owner.handle;pair[1]=seed.owner.control;return output;
        }
        assert(false);return nullptr;
    }
    static void reserve(void* group) {
        auto& f=*current;
        for(unsigned i=0;i<3;++i)if(reinterpret_cast<na::Address>(group)==f.manager(i)+32) {
            ++f.reserves;na::Address old=0;int count=0;
            assert(Fixture::read(&f,f.manager(i)+32,&old,8) && Fixture::read(&f,f.manager(i)+40,&count,4));
            const auto payload=f.alloc(static_cast<std::size_t>(count+1)*16);
            if(count)assert(Fixture::read(&f,old,f.memory.at(payload).data(),static_cast<std::size_t>(count)*16));
            f.put(f.manager(i)+32,payload);f.put(f.manager(i)+44,count+1);return;
        }
        assert(false);
    }
    static void append(void* manager,unsigned char group,std::uint64_t association,void* object) {
        auto& f=*current;assert(group==0 || group==2);
        for(unsigned i=0;i<3;++i)if(reinterpret_cast<na::Address>(manager)==f.manager(i)) {
            if(group==0) {
                assert(reinterpret_cast<na::Address>(object)==f.seeds[i].stock.objects[0]);
                const auto payload=f.alloc(16);f.put(payload,f.index(f.seeds[i].stock.objects[0]));
                f.put(payload+4,7);f.put(payload+8,association);
                f.put(f.manager(i),payload);f.put(f.manager(i)+8,1);f.put(f.manager(i)+12,1);return;
            }
            assert(reinterpret_cast<na::Address>(object)==f.seeds[i].candidate.objects[0]);
            f.seeds[i].association=association;
            na::Address payload=0;int count=0,capacity=0;
            assert(Fixture::read(&f,f.manager(i)+32,&payload,8) && Fixture::read(&f,f.manager(i)+40,&count,4)
                && Fixture::read(&f,f.manager(i)+44,&capacity,4));assert(count<capacity);
            const int index=f.index(f.seeds[i].candidate.objects[0]);f.put(f.items+index*24+16,19);
            f.put(payload+count*16,index);f.put(payload+count*16+4,19);f.put(payload+count*16+8,association);
            f.put(f.manager(i)+40,count+1);
            if(f.incomplete_bridge==static_cast<int>(i))f.put(f.manager(i)+40,0);
            return;
        }
        assert(false);
    }
    template<unsigned Role> static void bridge(void* object,unsigned char group,std::uint64_t association) {
        auto& f=*current;assert(!f.engine_locked);++f.bridges;
        // Actual production detour reentry under the writer must not self-deadlock.
        na::append_hook(reinterpret_cast<void*>(f.manager(Role)),group,association,object);
    }
    static void stock_reset(void* manager) {
        auto& f=*current;++f.stock_resets;
        for(unsigned role=0;role<3;++role)if(reinterpret_cast<na::Address>(manager)==f.manager(role)) {
            std::array<unsigned char,48> empty{};f.put(f.manager(role),empty);return;
        }
        assert(false);
    }
    static void stock_remove(void*,unsigned char,std::uint64_t,void*) {++current->stock_removes;}
    static std::uint64_t first_stock_name(void*,std::uint64_t name,void* manager) {
        auto& f=*current;
        for(unsigned role=0;role<3;++role)if(reinterpret_cast<na::Address>(manager)==f.manager(role)) {
            na::reset_hook(manager);
            na::append_hook(manager,0,f.seeds[role].stock_association,reinterpret_cast<void*>(f.seeds[role].stock.objects[0]));
            return name;
        }
        assert(false);return 0;
    }
    template<unsigned Role> static na::NativeAccessor* query(na::NativeAccessor* out,std::uint64_t key) {
        auto& f=*current;*out={};
        const auto new_key=f.work->name(Role==0?f.artifacts->bgm_key:f.artifacts->score_name);
        int count=0;assert(Fixture::read(&f,f.manager(Role)+40,&count,4));
        na::Address payload=0;assert(Fixture::read(&f,f.manager(Role)+32,&payload,8));
        bool registered=false;
        for(int n=0;n<count;++n){int index=-1;assert(Fixture::read(&f,payload+n*16,&index,4));registered|=index==f.index(f.seeds[Role].candidate.objects[0]);}
        if(key==new_key && registered) {
            out->words[1]=f.new_rows[Role];out->words[2]=f.seeds[Role].association;
            f.put(f.new_rows[0]+0x50,std::uint64_t{0xbad000002});
            f.put(f.new_rows[0]+bgm_resource,f.work->name(f.artifacts->music_name));
            f.put(f.new_rows[1]+80,f.work->name(f.artifacts->bgm_key));
        }else if(key==(Role==0?f.stock_bgm:f.stock_score)) {
            out->words[1]=f.stock_rows[Role];out->words[2]=f.name("Resident");
        }
        return out;
    }
    static na::NativeAccessor* score(na::NativeAccessor* out,std::uint64_t key){return query<1>(out,key);}
    static na::Address entry(std::string_view id) {
#define ENTRY(name,fn) if(id==name)return reinterpret_cast<na::Address>(&fn)
        ENTRY("fname_ctor",name_ctor);ENTRY("io_dispatcher_mount",mount);ENTRY("single_soft_path_request",acquire);
        ENTRY("bgmlist_accessor",query<0>);ENTRY("pianoscore_accessor",query<2>);
        ENTRY("bgmlist_append_bridge",bridge<0>);ENTRY("scoreinfo_append_bridge",bridge<1>);ENTRY("pianoscore_append_bridge",bridge<2>);
#undef ENTRY
        assert(false);return 0;
    }
    void frame(){na::updater_hook(nullptr);}
    void complete(){for(auto& s:seeds)put(s.owner.handle+16,static_cast<unsigned char>(1));}
    void start(bool ready_immediately) {
        immediate=ready_immediately;na::factory_hook();assert(na::g_captured.load());
        frame(); // No getter/activation observation is required for readiness.
        assert(mounts==1 && acquires==3 && !bridges);
    }
    void tracked_hooks() {
        na::g_installed=12;
        na::test_disable_hook=[](std::size_t){++current->disabled;return true;};
        na::test_remove_hook=[](std::size_t){++current->removed;return true;};
    }
    bool install_cold(bool missed=false) {
        // The synthetic image is never executable. Only installation and original
        // forwarding doubles run; all baselines use the production bounded reader.
        memory[base].resize(4096);
        IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=128;put(base,dos);
        IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt.OptionalHeader.AddressOfEntryPoint=rva::NativeAudioExeEntry;put(base+128,nt);
        for(auto address:{base+rva::NativeAudioCrtState,base+rva::NativeAudioInitialGuHeader,
            manager(0),manager(1),manager(2),base+rva::DataObjectNameMap}) {
            if(memory.contains(address))warm_headers.emplace_back(address,memory[address]);
            memory[address].assign(address==base+rva::NativeAudioCrtState?4:address==base+rva::DataObjectNameMap?8:48,0);
        }
        na::g_base=0;na::g_origin.store(false);na::g_entry_seen.store(false);na::g_hooks_ready.store(false);
        na::g_accepting.store(false);na::g_failure.store(na::RuntimeFailure::None);
        if(missed)put(base+rva::NativeAudioCrtState,1u);
        na::test_install_hook=[](std::string_view id) {
            auto& f=*current;const auto index=f.installed++;
            if(!index)assert(id=="native_audio_exe_entry");
            if(index==1 && f.original_before_barrier) {
                assert(na::ordinary_entry_hook()==17); // Actual AEP during partial installation.
                lookup(reinterpret_cast<void*>(f.manager(0)),0,nullptr,nullptr); // Original not yet intercepted.
            }
            if(static_cast<int>(index)==f.fail_install)return false;
            if(static_cast<int>(index)==f.entry_during_install)assert(na::ordinary_entry_hook()==17);
            if(static_cast<int>(index)==f.activity_during_install)na::lookup_hook(reinterpret_cast<void*>(f.manager(0)),0,nullptr,nullptr);
            return true;
        };
        na::test_disable_hook=[](std::size_t){++current->disabled;return true;};
        na::test_remove_hook=[](std::size_t){++current->removed;return true;};
        return na::install_native_audio_runtime(reinterpret_cast<HMODULE>(base),na::g_services);
    }
};

// Synthetic scalar/call seam exercising the actual production hook/traversal.
// Native access faults are represented by their fault address, never by a miss.
struct LookupScenario {
    struct Event {unsigned kind;na::Address target,receiver;std::uint64_t key;};
    inline static LookupScenario* active=nullptr;
    Fixture& f;
    decltype(na::test_memory) saved_memory=na::test_memory;
    decltype(na::original_lookup) saved_original=na::original_lookup;
    na::Address saved_base=na::g_base;
    bool saved_capability=na::g_lookup_dispatch;
    std::vector<Event> events;
    std::function<void(Event)> before;
    na::Address row=0xfeed0011;
    unsigned char readiness=1;
    explicit LookupScenario(Fixture& fixture):f(fixture) {
        assert(!active);active=this;na::g_base=Fixture::base;
        na::test_memory={&f,Fixture::read};na::test_lookup_call=call;
        na::original_lookup=opaque;na::g_lookup_dispatch=true;
        f.memory[Fixture::base+rva::NativeAudioInitialGuHeader].resize(48);
        f.put(Fixture::base+rva::NativeAudioInitialGuHeader+16,f.chunks);
        f.put(Fixture::base+rva::NativeAudioInitialGuHeader+36,128);
        f.memory[Fixture::base+rva::NativeBootstrapSrwPoolComplete].resize(1);
        f.put(Fixture::base+rva::NativeBootstrapSrwPoolComplete,static_cast<unsigned char>(1));
        f.memory[Fixture::base+rva::LookupBackingGetter]={0x48,0x8b,0x41,0x58,0xc3};
        f.memory[Fixture::base+rva::LookupSerializedGetter]={0x48,0x8b,0x41,0x58,0x48,0x83,0xc0,0x58,0xc3};
        for(unsigned role=0;role<3;++role) {
            const auto vt=get<na::Address>(f.seeds[role].stock.objects[0]);
            f.put(vt+Fixture::backing_slot,Fixture::base+rva::LookupBackingGetter);
            f.put(vt+Fixture::interface_slot,Fixture::base+rva::LookupSerializedGetter);
            for(const auto* witness:{&f.seeds[role].stock,&f.seeds[role].candidate}) {
                const auto provider=witness->objects[0];
                if(!role) {
                    const auto interface=f.alloc(48),vtable=f.alloc(48);
                    f.put(provider+0x58,interface);f.put(interface,vtable);
                    f.put(vtable+0x28,Fixture::base+rva::LookupMode0Bgm);
                }
                const auto receiver=get<na::Address>(provider+0x58)+(role?0x58:0);
                f.put(get<na::Address>(receiver)+8,Fixture::base+rva::LookupReadyTrue);
            }
        }
    }
    ~LookupScenario(){na::test_memory=saved_memory;na::original_lookup=saved_original;
        na::test_lookup_call=nullptr;na::g_lookup_dispatch=saved_capability;na::g_base=saved_base;active=nullptr;}
    template<class T> T get(na::Address address) const {
        T value{};assert(Fixture::read(&f,address,&value,sizeof(value)));return value;
    }
    static na::Address call(unsigned kind,na::Address target,na::Address receiver,std::uint64_t key) {
        auto& s=*active;const Event event{kind,target,receiver,key};s.events.push_back(event);
        if(s.before)s.before(event);
        if(kind==0)return s.get<na::Address>(receiver+0x58)+(target==Fixture::base+rva::LookupSerializedGetter?0x58:0);
        if(kind==1)return s.readiness;
        return kind==2?s.row:0;
    }
    static void* opaque(void* manager,std::uint64_t key,std::uint64_t*,unsigned char*) {
        return reinterpret_cast<void*>(call(3,0,reinterpret_cast<na::Address>(manager),key));
    }
    struct Result {void* row;std::uint64_t association;unsigned char mode;};
    Result run(unsigned role=0) {
        std::uint64_t association=0xbad;unsigned char mode=0x7f;
        auto* result=na::lookup_hook(reinterpret_cast<void*>(f.manager(role)),0xaabbccdd11223344,&association,&mode);
        return {result,association,mode};
    }
};

void lookup_traversal_tests() {
    const auto saved_signature=na::test_signature;
    na::test_signature=+[](std::string_view){return true;};
    assert(na::lookup_dispatch_capability());
    const auto* layout=na::native_audio_layout(ff7r::piano::core::generated::kBuildId);
    assert(layout && layout->backing_slot==Fixture::backing_slot && layout->serialized_interface_slot==Fixture::interface_slot);
    static std::string_view rejected_signature;
    na::test_signature=+[](std::string_view id){return id!=rejected_signature;};
    for(const auto targets:{std::span<const na::LookupTarget>{na::lookup_getters},std::span<const na::LookupTarget>{na::lookup_readiness},
            std::span<const na::LookupTarget>{na::lookup_mode0},std::span<const na::LookupTarget>{na::lookup_serialized}})
        for(const auto& target:targets) {
            rejected_signature=target.id;assert(!na::lookup_dispatch_capability());
        }
    rejected_signature="lookup_hot_singleton";
    assert(!na::lookup_dispatch_capability());na::test_signature=saved_signature;
    assert(na::native_lookup_stack_outputs(0x1010,0x1020,0x1000,0x1100));
    assert(!na::native_lookup_stack_outputs(0x10fc,0x1020,0x1000,0x1100));
    assert(!na::native_lookup_stack_outputs(0x1010,0x1017,0x1000,0x1100));
    assert(!na::native_lookup_stack_outputs(0x1010,0x1100,0x1000,0x1100));
    {Fixture f;LookupScenario s(f);auto result=s.run();
     assert(result.row==reinterpret_cast<void*>(s.row) && result.association==f.seeds[0].stock_association && result.mode==0);
      assert(s.events.size()==3 && s.events[0].kind==0 && s.events[1].kind==1 && s.events[2].kind==2);
      assert(s.events[0].target==Fixture::base+rva::LookupBackingGetter);
     assert(s.events[2].key==0xaabbccdd11223344);}
    for(unsigned role:{1u,2u}) {
        Fixture f;LookupScenario s(f);const auto result=s.run(role);
        assert(result.row && result.association==f.seeds[role].stock_association && result.mode==1);
        assert(s.events.size()==3 && s.events[0].target==Fixture::base+rva::LookupSerializedGetter);
        assert(s.events[2].receiver==s.get<na::Address>(f.seeds[role].stock.objects[0]+0x58)+0x58);
    }
    for(const auto& target:na::lookup_readiness) {
        Fixture f;LookupScenario s(f);const auto receiver=s.get<na::Address>(f.seeds[0].stock.objects[0]+0x58);
        f.put(s.get<na::Address>(receiver)+8,Fixture::base+target.rva);
        assert(s.run().row && s.events.size()==3 && s.events[1].target==Fixture::base+target.rva);
    }
    // All compiler-inlined full methods and ordinary mode0/mode1 targets dispatch
    // once with the same receiver/name, not a second hash parser in the hook.
    for(const auto targets:{std::span<const na::LookupTarget>{na::lookup_mode0},std::span<const na::LookupTarget>{na::lookup_serialized}})
        for(const auto& target:targets) {
            Fixture f;LookupScenario s(f);const auto provider=f.seeds[0].stock.objects[0];
            const auto receiver=s.get<na::Address>(provider+0x58),vt=s.get<na::Address>(receiver);
            f.put(vt+0x28,Fixture::base+target.rva);auto result=s.run();
            assert(result.row==reinterpret_cast<void*>(s.row) && s.events.size()==3);
            assert(s.events.back().target==Fixture::base+target.rva && s.events.back().receiver==receiver);
        }
    {Fixture f;LookupScenario s(f);s.readiness=0;auto result=s.run();
     assert(!result.row && result.association==0 && result.mode==0x7f && s.events.size()==2);}
    {Fixture f;LookupScenario s(f);s.row=0;auto result=s.run();
     assert(!result.row && result.association==0 && result.mode==0x7f && s.events.size()==3);}
    // Readiness reload boundary: no repeated getter; invoke NEW lookup exactly once.
    {Fixture f;LookupScenario s(f);const auto provider=f.seeds[0].stock.objects[0];
     const auto receiver=s.get<na::Address>(provider+0x58),replacement=f.alloc(48);
     f.put(s.get<na::Address>(receiver)+8,na::Address{0x77770000});
     f.put(replacement+0x28,Fixture::base+rva::LookupInlineScore);
     s.before=[&](LookupScenario::Event e){if(e.kind==1)f.put(receiver,replacement);};
     assert(s.run().row && s.events.size()==3 && s.events.back().target==Fixture::base+rva::LookupInlineScore);}
    // Capture-once: target replacement inside a call must not change that call.
    {Fixture f;LookupScenario s(f);const auto provider=f.seeds[0].stock.objects[0];
     const auto receiver=s.get<na::Address>(provider+0x58),vt=s.get<na::Address>(receiver);
     s.before=[&](LookupScenario::Event e){if(e.kind==2){assert(e.target==Fixture::base+rva::LookupMode0Bgm);f.put(vt+0x28,na::Address{0x77770000});}};
     assert(s.run().row && s.events.size()==3);}
    // Live group count/data rereads after a miss; hit association and mode use
    // current entry/provider, not the original borrowed provider or normalized mode.
    {Fixture f;LookupScenario s(f);const auto first=f.seeds[0].stock.objects[0],second=f.seeds[0].candidate.objects[0];
     const auto third=f.seeds[1].stock.objects[0];
     const auto payload=f.alloc(32),hit=f.alloc(32);const auto second_index=f.index(second);
     f.put(f.items+second_index*24+16,7);f.put(payload,f.index(first));f.put(payload+4,7);
     f.put(payload+16,second_index);f.put(payload+20,7);f.put(hit+16,f.index(third));f.put(hit+20,7);
     f.put(hit+24,std::uint64_t{0x8877665544332211});unsigned calls=0;
     s.before=[&](LookupScenario::Event e){if(e.kind==2){
         if(++calls==1){f.put(f.manager(0),payload);f.put(f.manager(0)+8,2);s.row=0;}
         else {f.put(f.manager(0),hit);f.put(third+0x54,static_cast<unsigned char>(0xa5));s.row=0xfeed0011;}}};
     const auto result=s.run();assert(calls==2 && result.row && result.association==0x8877665544332211 && result.mode==0xa5);}
    // Group order and signed empty groups: group1 then group2, no snapshots.
    {Fixture f;LookupScenario s(f);f.put(f.manager(0)+8,-1);const auto provider=f.seeds[0].stock.objects[0];
     for(unsigned group=1;group<3;++group){const auto payload=f.alloc(16);f.put(payload,f.index(provider));f.put(payload+4,7);
         f.put(payload+8,std::uint64_t{group});f.put(f.manager(0)+group*16,payload);f.put(f.manager(0)+group*16+8,1);}
     unsigned calls=0;s.before=[&](LookupScenario::Event e){if(e.kind==2)s.row=++calls==2?0xfeed0011:0;};
     const auto result=s.run();assert(calls==2 && result.association==2);}
    // Invalid weak inputs preserve native faults, including hit-time re-resolution.
    for(unsigned fault=0;fault<7;++fault) {
        Fixture f;LookupScenario s(f);const auto provider=f.seeds[0].stock.objects[0];
        const auto payload=s.get<na::Address>(f.manager(0));const auto index=f.index(provider);
        if(fault==0)f.put(payload+4,0);
        if(fault==1)f.put(payload,-1);
        if(fault==2)f.put(payload,128);
        if(fault==3)f.put(f.items+index*24+16,99);
        if(fault==4 || fault==5)f.put(f.items+index*24+8,std::uint32_t{1}<<(fault==4?28:29));
        if(fault==6)s.before=[&](LookupScenario::Event e){if(e.kind==2)f.put(payload+4,0);};
        bool faulted=false;try{(void)s.run();}catch(na::Address address){faulted=true;assert(address==(fault==6?0x54:0));}
        assert(faulted && (fault==6?s.events.size()==3:s.events.empty()));
    }
    {Fixture f;LookupScenario s(f);s.row=0;
     const auto provider=f.seeds[0].stock.objects[0],vt=s.get<na::Address>(provider);
     f.put(vt+Fixture::backing_slot,na::Address{0x77770000});
     assert(!s.run().row && s.events.size()==3 && s.events[0].target==0x77770000);}
    {Fixture f;LookupScenario s(f);s.row=0;unsigned effects=0;
     const auto receiver=s.get<na::Address>(f.seeds[0].stock.objects[0]+0x58);
     f.put(s.get<na::Address>(receiver)+0x28,na::Address{0x77770000});
     s.before=[&](LookupScenario::Event e){if(e.kind==2){++effects;const auto invalid=f.alloc(16);
         f.put(f.manager(0)+16,invalid);f.put(f.manager(0)+24,1);}};
     bool faulted=false;try{(void)s.run();}catch(na::Address address){faulted=true;assert(address==0);}
     assert(faulted && effects==1 && s.events.size()==3); // Never replay after effects.
    }
    // Non-owned/overlapping outputs and unknown manager fall back before traversal.
    {Fixture f;LookupScenario s(f);auto association=std::make_unique<std::uint64_t>();unsigned char mode=0;
     na::lookup_hook(reinterpret_cast<void*>(f.manager(0)),1,association.get(),&mode);
     assert(s.events.size()==1 && s.events[0].kind==3);s.events.clear();
     std::uint64_t stack=0;na::lookup_hook(reinterpret_cast<void*>(f.manager(0)),1,&stack,reinterpret_cast<unsigned char*>(&stack));
     assert(s.events.size()==1 && s.events[0].kind==3);s.events.clear();
     na::lookup_hook(reinterpret_cast<void*>(0x1234),1,&stack,&mode);assert(s.events.size()==1 && s.events[0].kind==3);}
}

void lookup_validation_tests() {
    // Baseline opaque original vs eligible actual dispatch, with FULL production
    // bindings validation. The baseline is deterministically excluded, no sleeps.
    for(unsigned scenario=0;scenario<9;++scenario) {
        RuntimeFixture f;f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
        LookupScenario s(f);const auto provider=f.seeds[0].stock.objects[0];
        const auto receiver=s.get<na::Address>(provider+0x58),vt=s.get<na::Address>(receiver);
        const bool accepted=scenario==1 || scenario==7 || scenario==8;
        if(scenario==0)na::g_lookup_dispatch=false;
        if(scenario==2)f.put(vt+0x28,na::Address{0x77770000});
        if(scenario==3 || scenario==7)f.put(vt+0x28,Fixture::base+rva::LookupInlinePiano);
        if(scenario==3)f.put(Fixture::base+rva::NativeBootstrapSrwPoolComplete,static_cast<unsigned char>(0));
        if(scenario==4)f.put(s.get<na::Address>(provider)+Fixture::backing_slot,na::Address{0x77770000});
        if(scenario==5)f.put(vt+8,na::Address{0x77770000});
        if(scenario==8)f.put(Fixture::base+rva::NativeBootstrapSrwPoolComplete,static_cast<unsigned char>(0));
        const auto saved_append=na::original_append;const auto saved_reserve=na::original_reserve;
        if(scenario==6) {
            na::original_reserve=+[](void*){};
            na::original_append=+[](void*,unsigned char,std::uint64_t,void*){na::reserve_hook(nullptr);};
        }
        std::latch entered{1},release{1};
        s.before=[&](LookupScenario::Event e){if(e.kind==2 || e.kind==3){
            if(scenario==6) {
                na::append_hook(nullptr,0,0,nullptr);
                // Mutation admission must still be held after nested return.
            }
            entered.count_down();release.wait();}};
        na::NativePianoScoreRow row{};na::NativePianoScoreCaptureFacts facts{};
        auto reads=f.reads;assert(na::capture_native_pianoscore_row(*f.artifacts,f.work->selected_name,row,&facts));
        const auto capture_reads=f.reads-reads;
        auto lookup=std::async(std::launch::async,[&]{return s.run();});entered.wait();reads=f.reads;
        assert(na::capture_native_pianoscore_row(*f.artifacts,f.work->selected_name,row,&facts)==accepted);
        if(accepted){assert(std::string_view(facts.predicate)=="accepted" && f.reads-reads==capture_reads);}
        else {assert(std::string_view(facts.predicate)=="manager_exclusion"
            && facts.manager_gate.reason==na::NativeEntryGateFailure::ExclusiveTryBusy);}
        release.count_down();lookup.get();na::original_append=saved_append;na::original_reserve=saved_reserve;
        assert(na::capture_native_pianoscore_row(*f.artifacts,f.work->selected_name,row,&facts));
        assert(scenario==0?s.events.size()==1:s.events.size()==3);
    }
    // An actual registration writer can reenter the original lookup and bridge
    // append->reserve without attempting an upgrade or acquiring mutation again.
    {Fixture f;LookupScenario s(f);na::NativeEntryGate::Writer registration{na::g_managers};assert(registration);
     assert(!s.run().row && s.events.size()==1 && s.events[0].kind==3);
     const auto append=na::original_append;const auto reserve=na::original_reserve;
     na::original_reserve=+[](void*){assert(na::NativeEntryGate::owns_registration());};
     na::original_append=+[](void*,unsigned char,std::uint64_t,void*){na::reserve_hook(nullptr);};
     na::append_hook(nullptr,0,0,nullptr);na::original_append=append;na::original_reserve=reserve;}
}
}

#ifndef FF7RP_NATIVE_AUDIO_RUNTIME_FIXTURE_ONLY
int main() {
    lookup_traversal_tests();lookup_validation_tests();
    for(unsigned unrelated:{0u,2048u})for(bool immediate:{false,true}) {
      RuntimeFixture f;
      if(unrelated) {
          const auto old=f.memory.at(f.items);f.items=f.alloc(4096*24);
          std::memcpy(f.memory.at(f.items).data(),old.data(),old.size());f.put(f.chunks,f.items);
          f.put(f.array+32,4096);f.put(f.array+36,4096);
          for(unsigned i=0;i<unrelated;++i)f.object(0,0,0,7);
      }
      f.start(immediate);const auto name_calls=f.name_calls;
      if(!immediate) {
          f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Acquiring);
          assert(f.mounts==1 && f.acquires==3 && !f.bridges && f.name_calls==name_calls);
          f.complete();
      }
      na::seed_names(*f.work);
      std::array<std::uint64_t,32> names{};
      for(std::size_t i=0;i<f.work->name_count;++i)names[i]=f.work->names[i].packed;
      f.reads=f.copied_bytes=0;assert(na::discover(*f.work,f.access()));
      std::cout<<"discovery unrelated="<<unrelated<<" immediate="<<immediate<<" reads="<<f.reads
          <<" reader_copied_bytes="<<f.copied_bytes<<'\n';
      for(unsigned role=0;role<3;++role) {
          assert(f.work->seeds[role].candidate.objects==f.seeds[role].candidate.objects);
          assert(f.work->seeds[role].stock.objects==f.seeds[role].stock.objects);
      }
      for(std::size_t i=0;i<f.work->name_count;++i)assert(names[i]==f.work->names[i].packed);
      assert(f.mounts==1 && f.acquires==3 && !f.bridges && f.name_calls==name_calls);
      f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
      assert(f.mounts==1 && f.acquires==3 && f.bridges==3 && f.name_calls==name_calls);
    }
    for(unsigned defect=0;defect<5;++defect) {
      RuntimeFixture f;f.start(true);na::seed_names(*f.work);
      const auto object=f.seeds[0].candidate.objects[0],outer=f.seeds[0].candidate.objects[5];
      if(defect==0)f.object(f.seeds[0].candidate_names.instance,f.seeds[0].candidate.objects[1],outer,0);
      if(defect==1)f.put(object+32,na::Address{});
      if(defect==2)f.put(outer+24,std::uint64_t{});
      if(defect==3)f.unreadable=object+24;
      if(defect==4)f.unreadable=outer+24;
      assert(!na::discover(*f.work,f.access()));
      assert(f.mounts==1 && f.acquires==3 && !f.bridges && !f.work->bindings.captured());
    }
    for(unsigned delayed_frames:{0u,2u}) {
      RuntimeFixture f;f.put(f.array+36,126);f.start(true);
      assert(f.work->timings.mount_us>=0 && f.work->timings.request_us>=0);
      assert(f.work->timings.discover_us==-1 && f.work->timings.bindings_us==-1 && f.work->timings.register_us==-1);
      assert(f.work->reported_stage==na::RuntimeStage::Acquiring);
      for(unsigned attempt=0;attempt<delayed_frames;++attempt) {
          f.after_read=[](Fixture& f,na::Address a,std::size_t size) noexcept {
              if(a==f.seeds[0].candidate.objects[0] && size==40) {
                  int count=0;assert(Fixture::read(&f,f.array+36,&count,sizeof(count)));
                  f.put(f.array+36,count+1);f.after_read=nullptr;
              }
          };
          f.frame();const auto pending=na::native_audio_status(*f.work);
          assert(!f.after_read && pending.stage==na::RuntimeStage::Authenticating
              && pending.failure==na::RuntimeFailure::None && !f.work->bindings.captured());
          assert(na::g_accepting.load() && na::g_failure.load()==na::RuntimeFailure::None && !na::g_next);
          std::uint64_t name=1;na::NativePianoScoreRow row;
          assert(!na::native_audio_selected_name(*f.work,41,name) && !name);
          assert(!na::capture_native_pianoscore_row(*f.artifacts,1,row) && !row.row);
          assert(f.mounts==1 && f.acquires==3 && !f.reserves && !f.bridges);
          assert(f.work->timings.discover_us>=0 && f.work->timings.bindings_us>=0 && f.work->timings.register_us==-1);
          assert(f.work->reported_stage==na::RuntimeStage::Authenticating);
      }
      f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
      assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
      assert(f.work->timings.discover_us>=0 && f.work->timings.bindings_us>=0 && f.work->timings.register_us>=0);
      assert(f.work->reported_stage==na::RuntimeStage::Ready);
      const auto ready_timings=f.work->timings;
      // Global growth after registration is not provider drift; pinned graphs still validate.
      f.put(f.array+36,128);na::NativePianoScoreRow row;
      assert(na::capture_native_pianoscore_row(*f.artifacts,f.work->selected_name,row));
      f.frame();assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
      assert(f.work->timings.mount_us==ready_timings.mount_us && f.work->timings.request_us==ready_timings.request_us
          && f.work->timings.discover_us==ready_timings.discover_us && f.work->timings.bindings_us==ready_timings.bindings_us
          && f.work->timings.register_us==ready_timings.register_us);
    }
    {RuntimeFixture f;f.start(true);f.frame();
     const auto selected=f.work->selected_name;
     na::NativePianoScoreRow plain,row;na::NativePianoScoreCaptureFacts facts;
     auto reads=f.reads;
     assert(na::capture_native_pianoscore_row(*f.artifacts,selected,plain));
     const auto success_reads=f.reads-reads;reads=f.reads;
     assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
     assert(f.reads-reads==success_reads && success_reads>0);
     assert(std::string_view(facts.predicate)=="accepted" && facts.generation==41
         && facts.status_observed && facts.binding_observed && facts.name_observed
         && facts.stage==na::RuntimeStage::Ready && facts.failure==na::RuntimeFailure::None
          && facts.binding_failure==na::BindingFailure::None
          && facts.manager_gate.reason==na::NativeEntryGateFailure::None
          && facts.manager_gate.observed_entry_mask==0
         && facts.requested_name==selected && facts.observed_name==selected);
     assert(row.provider==plain.provider && row.row==plain.row
         && row.selected_name==plain.selected_name && row.cohort_generation==plain.cohort_generation);
     // A foreign native entry excludes the capture without invalidating retained bindings.
     std::latch entered(1),release(1);
     auto reader=std::async(std::launch::async,[&]{
          na::NativeEntryGate::Reader gate{na::g_managers,na::NativeEntryKind::Lookup};entered.count_down();release.wait();
     });
     entered.wait();reads=f.reads;
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,plain));
     const auto busy_reads=f.reads-reads;reads=f.reads;
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      assert(f.reads-reads==busy_reads && std::string_view(facts.predicate)=="manager_exclusion"
          && facts.manager_gate.reason==na::NativeEntryGateFailure::ExclusiveTryBusy
          && facts.manager_gate.observed_entry_mask==(1u<<static_cast<unsigned>(na::NativeEntryKind::Lookup))
         && facts.status_observed && !facts.binding_observed && !facts.name_observed
         && !row.row && !row.provider);
      release.count_down();reader.get();
      assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      assert(facts.manager_gate.reason==na::NativeEntryGateFailure::None);
      // Nested readers inherit admission; only the outermost entry is observed.
      {
          na::NativeEntryGate::Reader outer{na::g_managers,na::NativeEntryKind::Append};
          {
              na::NativeEntryGate::Reader nested{na::g_managers,na::NativeEntryKind::Lookup};
              reads=f.reads;
              assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,plain));
              const auto nested_reads=f.reads-reads;reads=f.reads;
              assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
              assert(f.reads-reads==nested_reads && std::string_view(facts.predicate)=="manager_exclusion");
              assert(facts.manager_gate.reason==na::NativeEntryGateFailure::NestedEntry
                  && facts.manager_gate.observed_entry_mask==(1u<<static_cast<unsigned>(na::NativeEntryKind::Append)));
          }
          na::NativeEntryGate::Writer rejected{na::g_managers};assert(!rejected);
          assert(rejected.failure().reason==na::NativeEntryGateFailure::NestedEntry
              && rejected.failure().observed_entry_mask==(1u<<static_cast<unsigned>(na::NativeEntryKind::Append)));
      }
      // Both same-kind readers count independently; releasing one cannot clear the other.
      std::latch both_entered(2),release_first(1),release_second(1);
      auto first=std::async(std::launch::async,[&]{
          na::NativeEntryGate::Reader outer{na::g_managers,na::NativeEntryKind::Lookup};
          {na::NativeEntryGate::Reader nested{na::g_managers,na::NativeEntryKind::Lookup};}
          both_entered.count_down();release_first.wait();
      });
      auto second=std::async(std::launch::async,[&]{
          na::NativeEntryGate::Reader gate{na::g_managers,na::NativeEntryKind::Lookup};
          both_entered.count_down();release_second.wait();
      });
      both_entered.wait();
      assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      assert(facts.manager_gate.observed_entry_mask==(1u<<static_cast<unsigned>(na::NativeEntryKind::Lookup)));
      release_first.count_down();first.get();
      assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      assert(facts.manager_gate.reason==na::NativeEntryGateFailure::ExclusiveTryBusy
          && facts.manager_gate.observed_entry_mask==(1u<<static_cast<unsigned>(na::NativeEntryKind::Lookup)));
      release_second.count_down();second.get();
      assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      // A competing Writer is busy even with no observed reader categories.
      std::latch writer_entered(1),release_writer(1);
      auto writer=std::async(std::launch::async,[&]{
          na::NativeEntryGate::Writer gate{na::g_managers};assert(gate);
          writer_entered.count_down();release_writer.wait();
      });
      writer_entered.wait();reads=f.reads;
      assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,plain));
      const auto writer_reads=f.reads-reads;reads=f.reads;
      assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      assert(f.reads-reads==writer_reads && std::string_view(facts.predicate)=="manager_exclusion"
          && facts.manager_gate.reason==na::NativeEntryGateFailure::ExclusiveTryBusy
          && facts.manager_gate.observed_entry_mask==0);
      release_writer.count_down();writer.get();
      assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
      na::g_services.identity=+[](gu::UObjectIdentityAccess&) noexcept {return false;};
     reads=f.reads;assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,plain));
     const auto identity_reads=f.reads-reads;reads=f.reads;
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
     assert(f.reads-reads==identity_reads && std::string_view(facts.predicate)=="identity_service"
         && facts.status_observed && !facts.binding_observed && !facts.name_observed
         && !row.row && !row.provider);
     na::g_services.identity=RuntimeFixture::identity;
     assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row,&facts));
     assert(std::string_view(facts.predicate)=="accepted" && row.row==f.new_rows[2]);
     assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready
         && f.mounts==1 && f.acquires==3 && f.bridges==3);}
    {RuntimeFixture f;na::NativePianoScoreRow row;
     assert(!na::capture_native_pianoscore_row(*f.artifacts,1,row) && !row.row);
     f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
     const auto selected=f.work->selected_name;
     assert(na::capture_native_pianoscore_row(*f.artifacts,selected,row));
     assert(row.provider==f.seeds[2].candidate.objects[0] && row.row==f.new_rows[2]
         && row.row!=f.stock_rows[2] && row.selected_name==selected && row.cohort_generation==41);
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected+(1ull<<32),row) && !row.row);
     const auto copy=*f.artifacts;assert(!na::capture_native_pianoscore_row(copy,selected,row) && !row.row);
     const auto actual_row=f.work->candidate_rows[2].words[1];
     f.work->candidate_rows[2].words[1]=f.work->stock_rows[2].words[1];
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row) && !row.row);
     f.work->candidate_rows[2].words[1]=actual_row;
     f.put(f.closure+16,std::uint64_t{0});
     assert(!na::capture_native_pianoscore_row(*f.artifacts,selected,row) && !row.row);}
    for(auto id:{std::string("Song \xe6\x9b\xb2"),std::string(1024,'x')}) {
        RuntimeFixture f(std::move(id));f.start(true);f.frame();
        assert(na::native_audio_status(*f.work).stage==na::RuntimeStage::Ready);
        assert(f.work->names[f.work->name_count-1].text=="FF7RPianoSongs_"+f.artifacts->score_name);
    }
    using Stage=na::RuntimeStage;using Failure=na::RuntimeFailure;
    {RuntimeFixture f;f.zero_metadata();f.start(true);f.frame();
     const auto ready=na::native_audio_status(*f.work);assert(ready.stage==Stage::Ready && ready.failure==Failure::None);
     assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
     std::uint64_t name=0;assert(na::native_audio_selected_name(*f.work,f.work->generation,name) && name==f.work->selected_name);
     const auto metadata=f.seeds[2].candidate.objects[5];f.put(f.items+f.index(metadata)*24+16,23);
     assert(!na::native_audio_selected_name(*f.work,f.work->generation,name) && !name);
     const auto failed=na::native_audio_status(*f.work);
     assert(failed.failure==Failure::Bindings && f.mounts==1 && f.acquires==3 && f.bridges==3);
     const auto reads=f.reads;assert(na::shutdown_native_audio_runtime());
     (void)na::native_audio_status(*f.work);assert(f.reads==reads);
    }
    for(unsigned side=0;side<2;++side) {
     RuntimeFixture f;f.zero_metadata();f.start(true);
     const auto object=(side?f.seeds[0].stock:f.seeds[0].candidate).objects[5];
     f.put(f.items+f.index(object)*24+16,-5);f.frame();
     const auto failed=na::native_audio_status(*f.work);
     assert(failed.failure==Failure::Bindings && failed.binding.failure==na::BindingFailure::Identity && !f.bridges);
     assert(failed.binding.diagnostic.side==(side?na::BindingSide::Stock:na::BindingSide::Candidate));
     assert(failed.binding.diagnostic.node==5 && failed.binding.diagnostic.serial_observed && failed.binding.diagnostic.serial==-5);
     const auto reads=f.reads;const auto retained=na::native_audio_status(*f.work);assert(f.reads==reads);
     assert(retained.binding.diagnostic.identity_result==failed.binding.diagnostic.identity_result);
     f.frame();assert(f.mounts==1 && f.acquires==3 && !f.bridges && f.reads==reads);
     assert(na::shutdown_native_audio_runtime());
     const auto stopped=na::native_audio_status(*f.work);
     assert(stopped.binding.diagnostic.node==5 && stopped.binding.diagnostic.serial==-5 && f.reads==reads);
    }
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;
     assert(f.install_cold());assert(!f.notifications && !na::g_captured.load());
     assert(na::ordinary_entry_hook()==17);assert(!f.notifications);
     for(const auto& [address,bytes]:f.warm_headers)f.memory[address]=bytes;
     f.start(true);assert(f.notifications==1);f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready);
     assert(f.notifications==1 && na::g_initializer_notified.load());
     na::factory_hook();assert(f.notifications==1 && na::g_failure.load()==Failure::Replaced);}
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;
     f.put(Fixture::base+rva::PackageStoreOwnerGlobal,f.owner+8);f.put(Fixture::base+rva::PackageStoreGeneration,1u);
     na::initialize_hook(reinterpret_cast<void*>(f.owner));assert(f.notifications==1);
     na::initialize_hook(reinterpret_cast<void*>(f.owner));assert(f.notifications==1 && na::g_failure.load()==Failure::Replaced);}
    for(unsigned scenario=0;scenario<9;++scenario){RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;
     if(scenario==0)na::g_origin.store(false);
     if(scenario==1)na::invalidate(Failure::Replaced);
     if(scenario==2)na::g_accepting.store(false);
     if(scenario==3)f.put(f.closure+32,na::Address{0});
     if(scenario==4)f.duplicate_initialize=true;
     if(scenario==5)f.break_after_initialize=true;
     if(scenario==6)f.stop_after_initialize=true;
     if(scenario==7)f.throw_initialize=true;
     if(scenario==8)f.throw_factory=true;
     bool threw=false;try{na::factory_hook();}catch(const std::runtime_error&){threw=true;}
     assert(threw==(scenario>=7));assert(!f.notifications && !na::g_initializer_notified.load());
     assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls);
     if(scenario>=7)assert(na::g_failure.load()!=Failure::None);}
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;f.fail_install=4;
     assert(!f.install_cold());assert(!f.notifications && !na::g_initializer_notified.load() && !na::g_origin.load());}
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;f.tracked_hooks();f.stop_in_notification=true;
     na::factory_hook();assert(f.notifications==1 && !f.disabled && !f.removed && na::g_installed==12);
     assert(na::shutdown_native_audio_runtime() && f.disabled==12 && f.removed==12);}
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;f.reenter_notification=true;
     na::factory_hook();assert(f.notifications==1 && f.initializers==2 && na::g_failure.load()==Failure::Replaced);}
    {RuntimeFixture f;na::g_services.initializer_complete=RuntimeFixture::initialized;f.tracked_hooks();
     std::latch entered(1),release(1);f.notification_entered=&entered;f.notification_release=&release;
     auto running=std::async(std::launch::async,[]{na::factory_hook();});entered.wait();
     assert(f.notifications==1 && na::g_callbacks.callbacks_in_flight()==1);
     assert(!na::shutdown_native_audio_runtime() && !f.disabled && !f.removed && na::g_installed==12);
     release.count_down();running.get();assert(!na::g_callbacks.callbacks_in_flight());
     assert(na::shutdown_native_audio_runtime() && f.disabled==12 && f.removed==12);}
    {RuntimeFixture f;assert(f.install_cold());assert(na::ordinary_entry_hook()==17);
     for(const auto& [address,bytes]:f.warm_headers)f.memory[address]=bytes;
     for(unsigned role=0;role<3;++role)f.put(f.manager(role),std::array<unsigned char,48>{});
     na::original_reset=RuntimeFixture::stock_reset;na::original_remove=RuntimeFixture::stock_remove;
     na::original_name_helper=RuntimeFixture::first_stock_name;
     f.immediate=true;na::factory_hook();
     auto pending=[&] {
         f.frame();assert(na::native_audio_status(*f.work).stage==Stage::AwaitingStock);
         assert(!f.mounts && !f.acquires && !f.bridges && !f.name_calls && !f.work->mounted);
         assert(!na::g_stock_admitted.load() && na::g_next==0 && na::g_failure.load()==Failure::None);
     };
     pending();pending();
     // Normal first-provider helper reenters the actual reset/append detours.
     for(unsigned role=0;role<3;++role) {
         na::remove_hook(reinterpret_cast<void*>(f.manager(role)),0,0,nullptr);
         assert(na::name_helper_hook(reinterpret_cast<void*>(f.root),f.seeds[role].stock_names.instance,
             reinterpret_cast<void*>(f.manager(role)))==f.seeds[role].stock_names.instance);
         if(role<2){pending();pending();}
     }
     assert(f.stock_resets==3 && f.stock_removes==3 && !f.name_calls);
     f.frame();assert(na::g_stock_admitted.load() && f.mounts==1 && f.acquires==3 && f.name_calls && !f.bridges);
     f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready && f.bridges==3);
     f.frame();assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
    }
    for(unsigned role=0;role<3;++role) {
     RuntimeFixture f;f.put(f.manager(role)+8,0); // Allocated but empty is also valid Pending.
     f.immediate=true;na::factory_hook();f.frame();f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::AwaitingStock && !f.mounts && !f.name_calls);
     assert(!na::g_stock_admitted.load());f.put(f.manager(role)+8,1);f.frame();f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::Ready && f.mounts==1 && f.acquires==3 && f.bridges==3);
    }
    {RuntimeFixture f;std::latch entered(1),release(1);
     std::thread reader([&]{na::NativeEntryGate::Reader gate{na::g_managers};entered.count_down();release.wait();});
     entered.wait();f.immediate=true;na::factory_hook();f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::AwaitingStock && !na::g_stock_admitted.load());
     assert(!f.mounts && !f.name_calls);release.count_down();reader.join();f.frame();f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::Ready && f.mounts==1 && f.acquires==3 && f.bridges==3);}
    {RuntimeFixture f;f.start(true);na::original_reset=RuntimeFixture::stock_reset;
     na::original_name_helper=RuntimeFixture::first_stock_name;
     na::name_helper_hook(reinterpret_cast<void*>(f.root),f.seeds[0].stock_names.instance,reinterpret_cast<void*>(f.manager(0)));
     assert(na::native_audio_status(*f.work).failure==Failure::Bindings && f.stock_resets==1);
     f.frame();assert(f.mounts==1 && f.acquires==3 && !f.bridges);}
    for(bool reset:{false,true})for(bool ready:{false,true}) {
     RuntimeFixture f;f.start(true);if(ready)f.frame();assert(na::g_stock_admitted.load());
     na::original_reset=RuntimeFixture::stock_reset;na::original_remove=RuntimeFixture::stock_remove;
     if(reset)na::reset_hook(reinterpret_cast<void*>(f.manager(1)));
     else na::remove_hook(reinterpret_cast<void*>(f.manager(1)),0,0,nullptr);
     assert(na::native_audio_status(*f.work).failure==Failure::Bindings);
     assert(f.stock_resets+f.stock_removes==1);f.frame();assert(f.mounts==1 && f.acquires==3 && f.bridges==(ready?3u:0u));
    }
    for(unsigned role=0;role<3;++role)for(unsigned fault=0;fault<10;++fault) {
     RuntimeFixture f;na::Address payload=0;assert(Fixture::read(&f,f.manager(role),&payload,8));
     switch(fault) {
       case 0:f.put(f.manager(role)+8,-1);break;
       case 1:f.put(f.manager(role)+12,0);break;
       case 2:f.put(f.manager(role)+12,role?100001:4097);break;
       case 3:f.put(f.manager(role),na::Address{0x60000000});break; // unreadable span
       case 4:f.put(payload+4,0);break;
       case 5:f.put(payload+4,8);break; // stale weak
       case 6:f.drift=f.manager(role);break;
       case 7:f.drift=payload;break;
       case 8:f.put(f.seeds[role].stock.objects[0],na::Address{0});break;
       case 9:f.put(f.manager(role)+8,static_cast<int>(na::kNativeAudioEntryLimit)+1);break;
     }
     f.immediate=true;na::factory_hook();f.frame();const auto result=na::native_audio_status(*f.work);
     assert(result.failure==Failure::Bindings && result.role==role && result.binding.failure!=na::BindingFailure::None);
     assert(!na::g_stock_admitted.load() && !f.mounts && !f.acquires && !f.name_calls && !f.bridges && !f.work->mounted);
     f.frame();assert(!f.mounts && !f.name_calls);
    }
    {RuntimeFixture f;f.put(f.manager(0),std::array<unsigned char,16>{});
     f.put(f.manager(2)+8,-1);f.immediate=true;na::factory_hook();f.frame();
     assert(na::native_audio_status(*f.work).binding.failure==na::BindingFailure::RegistryBounds);
     assert(!f.mounts && !na::g_stock_admitted.load());} // Empty earlier class cannot mask a malformed later one.
    {RuntimeFixture f;
     auto changed=std::make_shared<ff7rp::pipeline::NativeAudioArtifactSet>(*f.artifacts);
     changed->base_score_name="DifferentStockCarrier";
     auto rejected=na::prepare_native_audio(changed,42);assert(rejected && !na::enqueue_native_audio(rejected));
     assert(na::g_count==1 && !rejected->resident);
     f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready);
     auto resident_mismatch=na::prepare_native_audio(changed,1000);
     assert(resident_mismatch && !na::enqueue_native_audio(resident_mismatch) && !resident_mismatch->resident);
     for(std::size_t n=1;n<na::kNativeAudioEntryLimit;++n) {
         auto work=na::prepare_native_audio(f.artifacts,41+n);assert(work && na::enqueue_native_audio(work));
         assert(work->resident==f.work);
     }
     assert(na::g_count==na::kNativeAudioEntryLimit);
     assert(na::enqueue_native_audio(f.work)); // Pointer idempotence even at the exact bound.
     auto extra=na::prepare_native_audio(f.artifacts,41+na::kNativeAudioEntryLimit);
     assert(extra && !na::enqueue_native_audio(extra) && !extra->resident && na::g_count==na::kNativeAudioEntryLimit);
     for(std::size_t n=0;n<na::kNativeAudioEntryLimit;++n)f.frame();
     assert(na::g_next==na::kNativeAudioEntryLimit && f.mounts==1 && f.acquires==3 && f.bridges==3);
     for(std::size_t n=0;n<na::g_count;++n)assert(na::native_audio_status(*na::g_queue[n]).stage==Stage::Ready);
    }
    for(const int count:{65,static_cast<int>(na::kNativeAudioEntryLimit)-1}) {
     RuntimeFixture f;
     for(unsigned role=0;role<3;++role)f.fill_group(role,2,count);
     std::array<std::vector<unsigned char>,3> prefixes;
     for(unsigned role=0;role<3;++role) {
         na::Address payload=0;assert(Fixture::read(&f,f.manager(role)+32,&payload,8));
         prefixes[role].resize(static_cast<std::size_t>(count)*16);
         assert(Fixture::read(&f,payload,prefixes[role].data(),prefixes[role].size()));
     }
     f.start(true);f.frame();
     const auto status=na::native_audio_status(*f.work);
     if(status.stage!=Stage::Ready)std::cerr<<"baseline="<<count<<" failure="<<na::native_audio_failure_name(status.failure)
         <<" binding="<<na::binding_failure_name(status.binding.failure)<<" role="<<int(status.role)<<'\n';
     assert(status.stage==Stage::Ready);
     assert(f.reserves==3 && f.bridges==3);
     for(unsigned role=0;role<3;++role) {
         int actual=0;na::Address payload=0;
         assert(Fixture::read(&f,f.manager(role)+40,&actual,4));assert(actual==count+1);
         assert(Fixture::read(&f,f.manager(role)+32,&payload,8));
         std::vector<unsigned char> prefix(prefixes[role].size());assert(Fixture::read(&f,payload,prefix.data(),prefix.size()));
         assert(prefix==prefixes[role]);
     }
     std::uint64_t name=0;assert(na::native_audio_selected_name(*f.work,41,name) && name);
    }
    {RuntimeFixture f;
     for(unsigned role=0;role<3;++role)f.fill_group(role,2,static_cast<int>(na::kNativeAudioEntryLimit));
     f.start(true);f.frame();assert(na::native_audio_status(*f.work).failure==Failure::Capacity);
     assert(!f.reserves && !f.bridges);}
    {RuntimeFixture f;f.fill_group(0,2,static_cast<int>(na::kNativeAudioEntryLimit)+1);
     f.start(true);f.frame();assert(na::native_audio_status(*f.work).binding.failure==na::BindingFailure::RegistryBounds);
     assert(!f.reserves && !f.bridges);}
    {RuntimeFixture f;std::uint64_t selected=9;
     assert(na::native_audio_status(*f.work).stage==Stage::AwaitingInitializer);
     assert(!na::native_audio_selected_name(*f.work,41,selected) && !selected);
     f.frame();assert(!f.mounts);f.start(false);f.frame();assert(!f.bridges);
     f.complete();f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready);
     assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
     for(unsigned i=0;i<3;++i){assert(na::native_audio_selected_name(*f.work,41,selected));assert(selected==f.work->selected_name);f.frame();}
     assert(!na::native_audio_selected_name(*f.work,42,selected));
     auto next=na::prepare_native_audio(f.artifacts,42);assert(next && na::enqueue_native_audio(next));
     assert(na::native_audio_selected_name(*next,42,selected));f.frame();
     assert(f.mounts==1 && f.acquires==3 && f.bridges==3);
     const auto write=CreateFileW(f.artifacts->files[0].path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
     assert(write==INVALID_HANDLE_VALUE); // Backing is immutable even across descriptor generations.
     f.put(f.root+12,4);assert(na::native_audio_selected_name(*f.work,41,selected));
     na::shutdown_hook();assert(!na::native_audio_selected_name(*f.work,41,selected));
     assert(na::native_audio_status(*f.work).failure==Failure::Shutdown);}
    {RuntimeFixture f;f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready);
     na::factory_hook();std::uint64_t selected=0;assert(!na::native_audio_selected_name(*f.work,41,selected));
     assert(na::native_audio_status(*f.work).failure==Failure::Replaced);f.frame();assert(f.mounts==1);}
    {RuntimeFixture f;f.start(false);f.put(f.seeds[2].owner.handle+17,static_cast<unsigned char>(1));f.frame();
     assert(na::native_audio_status(*f.work).failure==Failure::Acquisition);assert(!f.bridges);}
    {RuntimeFixture f;f.put(f.array+36,127);f.start(true);f.incomplete_bridge=1;
     f.after_read=[](Fixture& f,na::Address a,std::size_t) noexcept {
         if(current->bridges==2 && a==f.manager(1)+40) {
             f.put(f.array+36,128);f.after_read=nullptr;
         }
     };
     f.frame();assert(f.bridges==2 && !f.after_read);
     assert(na::native_audio_status(*f.work).stage==Stage::Failed);
     assert(f.work->bindings.terminal_failure().failure==na::BindingFailure::RegistryMembership);
     f.registered(1);f.frame();assert(f.bridges==2 && f.mounts==1);}
    {RuntimeFixture f;f.start(true);f.put(f.closure+16,std::uint64_t{78});f.frame();
     assert(na::native_audio_status(*f.work).failure==Failure::Provenance);assert(!f.bridges);}
    {RuntimeFixture f;f.put(f.package_nodes+24+8,na::Address{0});f.immediate=true;
     na::factory_hook();na::observe_native_stock_selected_name(f.stock_score);f.frame();
     assert(f.mounts==1 && !f.acquires && !f.bridges && f.work->mounted);
     assert(na::native_audio_status(*f.work).failure==Failure::Mount);f.frame();assert(f.mounts==1);}
    {RuntimeFixture f;f.start(true);na::destructor_hook(reinterpret_cast<void*>(f.owner));f.frame();
     assert(!f.bridges && na::native_audio_status(*f.work).failure==Failure::Shutdown);}
    {RuntimeFixture f;f.start(true);std::latch entered(1),release(1);
     std::thread reader([&]{na::NativeEntryGate::Reader gate{na::g_managers};entered.count_down();release.wait();});
     entered.wait();f.frame();assert(!f.bridges && na::native_audio_status(*f.work).stage==Stage::Acquiring);
     release.count_down();reader.join();f.frame();assert(f.bridges==3);}
    {RuntimeFixture f;f.start(true);assert(na::shutdown_native_audio_runtime());f.complete();f.frame();
     std::uint64_t selected=0;assert(!na::native_audio_selected_name(*f.work,41,selected));assert(!f.bridges);}
    {RuntimeFixture f;na::factory_hook();na::observe_native_stock_selected_name(f.stock_score);
     f.update_in_lookup=true;na::lookup_hook(reinterpret_cast<void*>(f.manager(0)),0,nullptr,nullptr);
     assert(!f.mounts && !f.acquires);f.update_in_lookup=false;f.frame();assert(f.mounts==1);}
    {RuntimeFixture f;na::test_entry=nullptr;
     assert(reinterpret_cast<na::Address>(na::entry<na::NameFn>("fname_ctor"))==Fixture::base+rva::FNameCtor);
     assert(!na::entry<na::NameFn>("unknown_callable"));na::test_entry=RuntimeFixture::entry;}
    {RuntimeFixture f;f.start(true);f.frame();std::latch seen(1);invalidation_seen=&seen;
     na::test_invalidation_entered=[]{invalidation_seen->count_down();};
     std::future<void> shutdown;
     {std::shared_lock reader(na::g_lifetime);
      shutdown=std::async(std::launch::async,[]{na::shutdown_hook();});seen.wait();
      assert(na::native_audio_status(*f.work).failure==Failure::Shutdown);
      assert(!f.shutdown_called.load());std::uint64_t selected=0;
      assert(!na::native_audio_selected_name(*f.work,41,selected));}
     shutdown.get();assert(f.shutdown_called.load());na::test_invalidation_entered=nullptr;invalidation_seen=nullptr;}
    {RuntimeFixture f;assert(na::enqueue_native_audio(f.work));f.start(true);f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::Ready);assert(na::enqueue_native_audio(f.work));
     f.frame();assert(f.mounts==1 && f.acquires==3 && f.bridges==3);}
    {RuntimeFixture f;assert(na::g_stock_selection.load()==0);f.start(true);f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::Ready);assert(na::g_stock_selection.load()==0);}
    {RuntimeFixture f;na::observe_native_stock_selected_name(f.name("UnrelatedRow"));f.start(true);f.frame();
     assert(na::native_audio_status(*f.work).stage==Stage::Ready);assert(f.work->stock_keys[1]==f.stock_score);}
    {RuntimeFixture f;auto bad=std::make_shared<ff7rp::pipeline::NativeAudioArtifactSet>(*f.artifacts);
     bad->base_score_name="MissingExistingRow";f.work=na::prepare_native_audio(bad,42);assert(f.work);
     na::g_queue[0]=f.work;f.immediate=true;na::factory_hook();f.frame();
     assert(!f.acquires && !f.bridges && na::native_audio_status(*f.work).stage==Stage::Failed);}
    {RuntimeFixture f;auto bad=std::make_shared<ff7rp::pipeline::NativeAudioArtifactSet>(*f.artifacts);
     bad->files[0].content_hash=ff7rp::pipeline::fnv1a64_string("fixture");assert(!na::prepare_native_audio(bad,42));
     bad->files[0].content_hash=f.artifacts->files[0].content_hash^1;assert(!na::prepare_native_audio(bad,42));
     bad->base_score_name.clear();assert(!na::prepare_native_audio(bad,42));}
    {RuntimeFixture f;const auto path=f.directory/L"hash.bin";
     std::vector<std::uint8_t> bytes(150003);for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i);
     {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
     const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
     assert(file!=INVALID_HANDLE_VALUE);const auto digest=ff7rp::pipeline::native_sha256(bytes);
     std::uint64_t expected=0,actual=0;for(unsigned j=0;j<8;++j)expected|=static_cast<std::uint64_t>(digest[j])<<(j*8);
     assert(na::compact_file_hash(file,bytes.size(),actual) && actual==expected);
     assert(!na::compact_file_hash(file,1,actual)); // EOF/partial read is not a digest.
     assert(!na::compact_file_hash(file,513ull*1024*1024,actual));CloseHandle(file);}
    for(bool replacement:{false,true}) {
     RuntimeFixture f;f.tracked_hooks();f.immediate=true;na::factory_hook();
     std::latch mounted(1),release(1),invalidator(1);f.mount_entered=&mounted;f.mount_release=&release;
     auto updater=std::async(std::launch::async,[&]{f.frame();});mounted.wait();
     assert(!na::shutdown_native_audio_runtime());assert(f.disabled==0 && f.removed==0 && na::g_callbacks.accepting());
     invalidation_seen=&invalidator;na::test_invalidation_entered=[]{invalidation_seen->count_down();};
     auto destructive=std::async(std::launch::async,[&]{if(replacement)na::factory_hook();else na::destructor_hook(reinterpret_cast<void*>(f.owner));});
     invalidator.wait();assert(f.disabled==0 && f.removed==0);release.count_down();updater.get();destructive.get();
     na::test_invalidation_entered=nullptr;invalidation_seen=nullptr;
     assert(!f.acquires && !f.bridges);assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);
    }
    {RuntimeFixture f;f.start(true);f.frame();f.tracked_hooks();
     std::latch entered(1),release(1);f.identity_entered=&entered;f.identity_release=&release;
     auto selection=std::async(std::launch::async,[&]{std::uint64_t name=0;return na::native_audio_selected_name(*f.work,41,name);});
     entered.wait();assert(!na::shutdown_native_audio_runtime());assert(!f.disabled && !f.removed);
     release.count_down();selection.get();f.identity_entered=nullptr;
     assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);}
    {RuntimeFixture f;assert(f.install_cold());assert(!na::g_origin.load());f.frame();assert(!f.mounts);
     assert(na::g_failure.load()==Failure::MissedInitializer);assert(na::ordinary_entry_hook()==17);assert(!na::g_origin.load());}
    {RuntimeFixture f;assert(f.install_cold());assert(na::ordinary_entry_hook()==17);assert(na::g_origin.load());
     assert(na::g_updater_thread.load()==GetCurrentThreadId());assert(na::g_callbacks.callbacks_in_flight()==0);
     for(const auto& [address,bytes]:f.warm_headers)f.memory[address]=bytes;
     f.start(true);f.frame();assert(na::native_audio_status(*f.work).stage==Stage::Ready);
     assert(na::g_stock_selection.load()==0 && f.bridges==3);}
    {RuntimeFixture f;f.tracked_hooks();f.stop_inside_mount=true;na::factory_hook();f.frame();
     assert(f.mounts==1 && !f.acquires && !f.bridges);assert(!f.disabled && !f.removed);
     assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);}
    for(auto rva:{rva::NativeAudioCrtState,rva::NativeAudioInitialGuHeader,rva::BgmListRegistry,
        rva::ScoreInfoRegistry,rva::PianoScoreRegistry,rva::DataObjectNameMap}) {
     RuntimeFixture f;assert(f.install_cold());f.put(Fixture::base+rva,static_cast<unsigned char>(1));
     assert(na::ordinary_entry_hook()==17);assert(!na::g_origin.load());f.frame();assert(!f.mounts);
    }
    {RuntimeFixture f;assert(!f.install_cold(true));assert(!f.installed && !na::g_origin.load());}
    {RuntimeFixture f;f.original_before_barrier=true;assert(!f.install_cold());assert(f.lookups==1);
     assert(!na::g_origin.load() && !f.mounts && !f.acquires && !f.bridges);assert(f.removed==12);}
    for(int index=0;index<12;++index) {
     RuntimeFixture f;f.entry_during_install=index;assert(!f.install_cold());assert(!na::g_origin.load());
     assert(f.disabled==12 && f.removed==12);
    }
    for(int index=1;index<12;++index) {
     RuntimeFixture f;f.activity_during_install=index;assert(!f.install_cold());assert(!na::g_origin.load());
     assert(f.disabled==12 && f.removed==12);
    }
    for(int index=0;index<12;++index) {
     RuntimeFixture f;f.fail_install=index;assert(!f.install_cold());assert(!na::g_origin.load());
     assert(f.disabled==static_cast<unsigned>(index) && f.removed==static_cast<unsigned>(index));
    }
    {RuntimeFixture f;assert(f.install_cold());assert(!f.entry_calls && !na::g_entry_forwarding.load());
     assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);}
    for(bool handoff:{false,true})for(bool failed_origin:{false,true}) {
     RuntimeFixture f;assert(f.install_cold());
     if(failed_origin)na::g_hooks_ready.store(false);
     std::latch entered(1),release(1);f.entry_entered=&entered;f.entry_release=&release;
     if(handoff) {
         na::test_entry_handoff=[]{current->entry_entered->count_down();current->entry_release->wait();};
         na::original_entry=[] {++current->entry_calls;assert(na::g_entry_forwarding.load()==1);return 17;};
     }
     auto entry=std::async(std::launch::async,[]{return na::ordinary_entry_hook();});entered.wait();
     assert(na::g_callbacks.callbacks_in_flight()==0 && na::g_entry_forwarding.load()==1);
     assert(f.entry_calls==(handoff?0u:1u));
     // Synchronous nonblocking result: neither a timeout nor an eventual stop is proof.
     assert(!na::shutdown_native_audio_runtime());assert(!f.disabled && !f.removed && na::g_installed==12);
     assert(na::g_callbacks.accepting());release.count_down();assert(entry.get()==17);
     assert(f.entry_calls==1 && na::g_entry_forwarding.load()==0);
     assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);
    }
    {RuntimeFixture f;assert(f.install_cold());
     {auto lease=na::g_callbacks.try_enter();assert(lease);assert(!na::shutdown_native_audio_runtime());}
     f.stop_inside_entry=true;assert(na::ordinary_entry_hook()==17);
     assert(!na::g_origin.load() && f.entry_calls==1 && !na::g_entry_forwarding.load());
     assert(na::shutdown_native_audio_runtime());assert(f.disabled==12 && f.removed==12);}
    {RuntimeFixture f;assert(f.install_cold());f.reenter_entry=true;f.stop_inside_entry=true;
     assert(na::ordinary_entry_hook()==17);assert(f.entry_calls==2 && !na::g_entry_forwarding.load());
     assert(na::g_failure.load()!=Failure::None);assert(na::shutdown_native_audio_runtime());}
    {RuntimeFixture f;assert(f.install_cold());na::g_entry_forwarding.store(na::kEntryForwardingLimit);
     assert(na::ordinary_entry_hook()==17);assert(f.entry_calls==1);
     assert(na::g_entry_forwarding.load()==na::kEntryForwardingLimit && na::g_entry_forwarding_uncertain.load());
     assert(!na::shutdown_native_audio_runtime() && !f.disabled && !f.removed);
     // Remove only the injected synthetic saturation; real uncertainty is sticky to exit.
     na::g_entry_forwarding.store(0);assert(!na::shutdown_native_audio_runtime());
     na::g_entry_forwarding_uncertain.store(false);assert(na::shutdown_native_audio_runtime());}
    std::cout<<"native_audio_runtime_selftest passed\n";
    return 0;
}
#endif
