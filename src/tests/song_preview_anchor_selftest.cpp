#include "game/song_preview_anchor.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

using namespace ff7r::piano::game;
static void require(bool v,const char* m) { if (!v) throw std::runtime_error(m); }
struct Pair { std::uintptr_t pointer, controller; };
struct Memory {
    std::map<std::uintptr_t,std::vector<std::byte>> regions;
    int reads=0, fail_at=0;
    std::uintptr_t last_address=0;
    std::function<void(int)> mutate;
    void add(std::uintptr_t address) { regions[address].resize(0x500); }
    template<class T> void put(std::uintptr_t address,const T& value) {
        auto i=regions.upper_bound(address); require(i!=regions.begin(),"fixture region"); --i;
        const auto offset=address-i->first;
        require(offset+sizeof(value)<=i->second.size(),"fixture write bounds");
        std::memcpy(i->second.data()+offset,&value,sizeof(value));
    }
    bool read(std::uintptr_t address,void* out,std::size_t size) {
        ++reads; last_address=address; if (mutate) mutate(reads); if (reads==fail_at) return false;
        auto i=regions.upper_bound(address); if (i==regions.begin()) return false; --i;
        const auto offset=address-i->first;
        if (offset>i->second.size() || size>i->second.size()-offset) return false;
        std::memcpy(out,i->second.data()+offset,size); return true;
    }
};
static PreviewPaintGeometry geometry(float x,float y,float w,float h) {
    PreviewPaintGeometry g; g.tx=x; g.ty=y; g.width=w; g.height=h; return g;
}
int main()
{
    try {
        const auto row=geometry(2512,964,992,116), list=geometry(2492,236,1032,844);
        const PreviewCullRect cull{2452,236,3544,1078};
        PreviewRootLayout root{0,0,3840,2160,2560,1440,1.33333325f};
        PreviewSelectedRowBounds out;
        require(map_preview_anchor_geometry(row,list,cull,root,2560,1440,out),"live bottom-row mapping");
        require(std::abs(out.row.x-1674.6667f)<.01f && std::abs(out.row.y-642.6667f)<.01f
            && std::abs(out.inherited_cull.x-1634.6667f)<.01f,"native inverse, no extra DPI");
        require(preview_badge_layout(out,2560,1440).has_value(),"observed margin fits24px badge");
        auto hd=root; hd.output_width=1920; hd.output_height=1080; hd.viewport_scale=1;
        require(map_preview_anchor_geometry(row,list,cull,hd,1920,1080,out)
            && preview_badge_layout(out,1920,1080),"same native mapping at1080p");
        auto top=row; top.ty=236;
        require(map_preview_anchor_geometry(top,list,cull,root,2560,1440,out)
            && preview_badge_layout(out,2560,1440),"first scroll position");
        top.ty=220;
        require(!map_preview_anchor_geometry(top,list,cull,root,2560,1440,out),"partial scroll hidden");
        auto mismatch=root; mismatch.origin_x=1;
        require(!map_preview_anchor_geometry(row,list,cull,mismatch,2560,1440,out),"nonzero origin unsupported");
        mismatch=root; mismatch.output_width=2550;
        require(!map_preview_anchor_geometry(row,list,cull,mismatch,2560,1440,out),"output mismatch");
        auto invalid=row; invalid.a=std::numeric_limits<float>::quiet_NaN();
        require(!map_preview_anchor_geometry(invalid,list,cull,root,2560,1440,out),"nonfinite affine");
        auto opaque=row; opaque.reserved.fill(std::numeric_limits<float>::quiet_NaN());
        opaque.tail=std::numeric_limits<float>::quiet_NaN();
        require(map_preview_anchor_geometry(opaque,list,cull,root,2560,1440,out),"opaque geometry padding is not scalar data");
        invalid=row; invalid.d=0;
        require(!map_preview_anchor_geometry(invalid,list,cull,root,2560,1440,out),"degenerate affine");
        // Rotation requires all four corners, not just origin and bottom-right.
        auto rotated=geometry(2512,964,116,992); rotated.a=0; rotated.b=1; rotated.c=1; rotated.d=0;
        require(map_preview_anchor_geometry(rotated,list,cull,root,2560,1440,out)
            && std::abs(out.row.width-661.3333f)<.01f,"four-corner affine mapping");

        Memory memory;
        for (auto p : {0x1000u,0x2000u,0x3000u,0x4000u,0x5000u,0x6000u,0x7000u,0x8000u,0x9000u}) memory.add(p);
        const Pair root_pair{0x2000,0x3000}, row_pair{0x6000,0x7000}, list_pair{0x8000,0x9000};
        // Synthetic layout offset; production must supply its independently gated native offset.
        const PreviewAnchorSource source{0x1000,0x4000,0x5000,8};
        memory.put(0x1000,root_pair); memory.put(0x2318,root);
        for (auto p : {0x3008u,0x7008u,0x9008u}) memory.put(p,std::int32_t{1});
        memory.put(0x40e8,row_pair); memory.put(0x50d8,list_pair);
        memory.put(0x6090,row); memory.put(0x6100,cull);
        memory.put(0x8090,list); memory.put(0x8100,cull);
        memory.put(0x6038,list_pair); memory.put(0x8038,root_pair);
        const PreviewAnchorRead read=[&](auto a,auto o,auto s){return memory.read(a,o,s);};
        require(read_preview_anchor(source,read,[]{return true;},2560,1440,out),"preferred + fallback shared widgets");
        const int successful_reads=memory.reads;
        memory.put(0x6038,root_pair);
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"row under same root but unrelated to active list");
        memory.put(0x6038,list_pair);
        for (int n=1;n<=successful_reads;++n) {
            memory.reads=0; memory.fail_at=n;
            require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"every failed native copy hides");
            require(out.row.width==0,"failure clears output");
        }
        memory.fail_at=0; memory.reads=0;
        std::array<Pair,30> chain{};
        for (std::size_t i=0;i<chain.size();++i) {
            const auto node=0x20000+i*0x1000;
            chain[i]={node,node+0x800};
            memory.add(node); memory.add(node+0x800);
            memory.put(node+0x808,std::int32_t{1});
        }
        for (std::size_t i=0;i+1<chain.size();++i) memory.put(chain[i].pointer+0x38,chain[i+1]);
        memory.put(chain[28].pointer+0x38,root_pair); memory.put(0x8038,chain[0]);
        require(read_preview_anchor(source,read,[]{return true;},2560,1440,out),"32-node ancestry admitted");
        memory.put(chain[28].pointer+0x38,chain[29]); memory.put(chain[29].pointer+0x38,root_pair);
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"33-node ancestry fails bounded capture");
        memory.put(0x8038,root_pair);
        int identities=0;
        require(!read_preview_anchor(source,read,[&]{return ++identities==1;},2560,1440,out),"stale owning selection/session");
        memory.put(0x6038,row_pair);
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"cyclic ancestry");
        memory.put(0x6038,list_pair); memory.put(0x8038,row_pair);
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"unrelated root");
        memory.put(0x8038,root_pair);
        memory.put(0x300c,std::int32_t{100});
        memory.put(0x3008,std::int32_t{0});
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"positive weak count cannot rescue expired root");
        memory.put(0x3008,std::int32_t{1});
        memory.add(0xa000);
        memory.put(0x40d8,row_pair); memory.put(0x40e8,Pair{0x6000,0xa000});
        memory.put(0xa008,std::int32_t{0});
        require(read_preview_anchor(source,read,[]{return true;},2560,1440,out),"expired preferred uses fallback");
        int preferred_reads=0;
        memory.mutate=[&](int) {if(memory.last_address==0xa008 && ++preferred_reads==2)
            memory.put(0xa008,std::int32_t{1});};
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"resolver changes despite stable pair bytes");
        memory.mutate={};
        memory.put(0x40d8,Pair{}); memory.put(0x40e8,row_pair);
        memory.reads=0;
        int pair_reads=0;
        memory.mutate=[&](int) {if(memory.last_address==0x40d8 && ++pair_reads==2) memory.put(0x40e8,Pair{});};
        require(!read_preview_anchor(source,read,[]{return true;},2560,1440,out),"widget pair changes during copy");
        memory.mutate={};
        PreviewAnchorGate gate;
        gate.root_pointer=0x1000; gate.root_controller=0x1008;
        for (std::size_t i=0;i<gate.verifiers.size();++i) {
            auto& verifier=gate.verifiers[i];
            verifier.name="test_verifier";
            verifier.address=0x11000+i*0x1000;
            verifier.bytes.assign(i==0 ? 48 : 32,static_cast<std::uint8_t>(0x40+i));
            memory.add(verifier.address);
        }
        auto& getter=gate.verifiers.front();
        const auto relative=[&](std::size_t offset,std::size_t end,std::uintptr_t target) {
            const auto disp=static_cast<std::int32_t>(static_cast<std::int64_t>(target)
                -static_cast<std::int64_t>(getter.address+end));
            std::memcpy(getter.bytes.data()+offset,&disp,sizeof(disp));
        };
        relative(9,0xd,gate.root_controller); relative(0x2b,0x2f,gate.root_pointer);
        for (const auto& v:gate.verifiers) {
            for (std::size_t i=0;i<v.bytes.size();++i) memory.put(v.address+i,v.bytes[i]);
        }
        std::string_view failure;
        require(validate_preview_anchor_gate(gate,read,failure) && failure.empty(),"all gates + signed RIP targets");
        for (std::size_t i=0;i<gate.verifiers.size();++i) {
            auto incomplete=gate; incomplete.verifiers[i]={};
            require(!validate_preview_anchor_gate(incomplete,read,failure),"any absent optional verifier hides geometry");
        }
        const PreviewAnchorRead throwing_read=[](auto,auto,auto)->bool {
            throw std::runtime_error("injected optional geometry read failure");
        };
        require(!validate_preview_anchor_gate(gate,throwing_read,failure),"gate read exceptions stay inside optional UI");
        require(!read_preview_anchor(source,throwing_read,[]{return true;},2560,1440,out)
            && out.row.width==0,"layout read exceptions clear badge without escaping");
        require(validate_preview_anchor_gate(gate,read,failure),"failed optional checks do not poison subsequent validation");
        for (const auto& v:gate.verifiers) {
            memory.put(v.address,static_cast<std::uint8_t>(v.bytes[0]^1));
            require(!validate_preview_anchor_gate(gate,read,failure),"each corrupted signature disables optional geometry");
            memory.put(v.address,v.bytes[0]);
        }
        memory.put(getter.address+0x2b,static_cast<std::uint8_t>(getter.bytes[0x2b]^1));
        require(!validate_preview_anchor_gate(gate,read,failure),"second RIP instruction outside32-byte prefix is gated");
        memory.put(getter.address+0x2b,getter.bytes[0x2b]);
        auto wrong=gate; wrong.root_pointer+=16; wrong.root_controller+=16;
        require(!validate_preview_anchor_gate(wrong,read,failure) && failure=="root_getter_rip_targets","canonical global disagreement");
        const int before_unsupported=memory.reads;
        require(!validate_preview_anchor_gate({},read,failure) && memory.reads==before_unsupported,
            "unsupported build absent gates fail closed without native reads");
        std::puts("song preview anchor selftest passed"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
