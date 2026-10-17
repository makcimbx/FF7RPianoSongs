#include "native_iostore_writer.h"
#include "native_asset_hash.h"
#include <algorithm>
#include <map>
#include <set>

namespace ff7rp::pipeline {
using namespace native_binary;
namespace {
constexpr std::uint32_t none=0xffffffff;
Status bad(const char* s) { return Status::error(StatusCode::InvalidArgument,s); }
// Pinned retoc 234cd0d: directory resource is FString, directories[4*u32],
// files[3*u32], FString table. None links are UINT32_MAX.
Bytes directory(const std::array<NativeZenPackage,4>& packages) {
    std::vector<std::array<std::uint32_t,4>> dirs(1,{none,none,none,none});
    std::vector<std::array<std::uint32_t,3>> files;
    std::vector<std::string> names;
    auto intern=[&](const std::string& n) {
        auto i=std::find(names.begin(),names.end(),n); if(i==names.end()) { names.push_back(n); return std::uint32_t(names.size()-1); } return std::uint32_t(i-names.begin());
    };
    for(std::uint32_t i=0;i<4;++i) {
        std::string path="End/Content/"+packages[i].package_name.substr(6)+".uasset";
        std::uint32_t parent=0; std::size_t at=0;
        for(;;) {
            auto slash=path.find('/',at); auto part=path.substr(at,slash==std::string::npos?slash:slash-at); auto name=intern(part);
            if(slash==std::string::npos) {
                auto index=std::uint32_t(files.size()); files.push_back({name,none,i});
                if(dirs[parent][3]==none) dirs[parent][3]=index;
                else { auto last=dirs[parent][3]; while(files[last][1]!=none) last=files[last][1]; files[last][1]=index; } break;
            }
            auto child=dirs[parent][1],last=none;
            while(child!=none && dirs[child][0]!=name) { last=child; child=dirs[child][2]; }
            if(child==none) {
                child=std::uint32_t(dirs.size()); dirs.push_back({name,none,none,none});
                if(last==none) dirs[parent][1]=child; else dirs[last][2]=child;
            }
            parent=child; at=slash+1;
        }
    }
    Bytes b; string(b,"../../../"); add(b,dirs.size()); for(auto d:dirs) for(auto v:d) add(b,v);
    add(b,files.size()); for(auto f:files) for(auto v:f) add(b,v);
    add(b,names.size()); for(auto& s:names) string(b,s); return b;
}
Bytes store_header(std::uint64_t id,const std::array<NativeZenPackage,4>& packages,const std::array<NativeIoStorePackageInput,4>& inputs) {
    Bytes b; add(b,id,8); add(b,4); add(b,0); add(b,0); add(b,8); add(b,0xc1640000,8);
    std::map<std::uint64_t,std::size_t> order; for(std::size_t i=0;i<4;++i) order.emplace(packages[i].package_id,i);
    add(b,4); for(auto [pid,i]:order) { (void)i; add(b,pid,8); }
    add(b,4*32);
    for(auto [pid,i]:order) {
        (void)pid; add(b,inputs[i].zen.size(),8); add(b,1); add(b,packages[i].export_bundle_count);
        add(b,0); add(b,none); add(b,0); add(b,0);
    }
    add(b,0); add(b,0); b.resize((b.size()+15)&~std::size_t(15)); return b;
}
}
NativeIoStoreBuildResult build_native_iostore(const std::string& name,const std::array<NativeIoStorePackageInput,4>& inputs) {
    NativeIoStoreBuildResult result;
    if(name.empty()||name.size()>128||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos) { result.status=bad("Unsafe native container name"); return result; }
    std::array<NativeZenPackage,4> packages; std::set<std::uint64_t> ids; std::set<std::string> paths;
    std::size_t total=0;
    for(std::size_t i=0;i<4;++i) {
        if(inputs[i].role!=NativeAudioPackageRole(i)) { result.status=bad("Native cohort roles/order"); return result; }
        result.status=inspect_native_zen_package(inputs[i].zen,inputs[i].role,packages[i]); if(!result.status.ok()) return result;
        const char* canonical[]{"","BGMList","PianoScoreInfo","PianoScore"};
        if((i && packages[i].export_name!=canonical[i]) || !ids.insert(packages[i].package_id).second || !paths.insert(packages[i].package_name).second) { result.status=bad("Duplicate package/canonical DataObject basename"); return result; }
        total+=inputs[i].zen.size(); if(total>128*1024*1024) { result.status=bad("Native cohort size limit"); return result; }
    }
    const auto id=native_name_hash(name,true); if(ids.contains(id)) { result.status=bad("Container/package chunk identity collision"); return result; }
    Bytes cas,blocks,offsets,chunkids,meta;
    auto chunk=[&](std::uint64_t chunk_id,std::uint8_t type,const Bytes& data) {
        add(chunkids,chunk_id,8); add(chunkids,0,3); chunkids.push_back(type);
        auto offset=std::uint64_t(blocks.size()/12)*65536;
        for(int i=4;i>=0;--i) offsets.push_back(std::uint8_t(offset>>(i*8)));
        for(int i=4;i>=0;--i) offsets.push_back(std::uint8_t(std::uint64_t(data.size())>>(i*8)));
        for(std::size_t at=0;at<data.size();at+=65536) {
            const auto n=std::min<std::size_t>(65536,data.size()-at);
            add(blocks,cas.size(),5); add(blocks,n,3); add(blocks,n,3); blocks.push_back(0); append(cas,std::span(data).subspan(at,n));
        }
        auto hash=native_blake3(data); meta.insert(meta.end(),hash.begin(),hash.begin()+20); meta.resize(meta.size()+13);
    };
    for(std::size_t i=0;i<4;++i) chunk(packages[i].package_id,2,inputs[i].zen);
    auto store=store_header(id,packages,inputs); chunk(id,10,store);
    auto dir=directory(packages); Bytes toc(144);
    const std::string magic="-==--==--==--==-"; std::copy(magic.begin(),magic.end(),toc.begin()); toc[16]=2;
    put(toc,20,144); put(toc,24,5); put(toc,28,blocks.size()/12); put(toc,32,12); put(toc,40,32); put(toc,44,65536); put(toc,48,dir.size()); put(toc,52,1); put(toc,56,id,8); toc[80]=8; put(toc,88,~std::uint64_t(0),8);
    append(toc,chunkids); append(toc,offsets); append(toc,blocks); append(toc,dir); append(toc,meta);
    result.utoc=std::move(toc); result.ucas=std::move(cas); result.container_id=id;
    for(std::size_t i=0;i<4;++i) result.packages[i]={inputs[i].role,packages[i].package_name+"."+packages[i].export_name,packages[i].class_path,packages[i].package_id,packages[i].public_export_hash};
    result.status=Status::ok_status(); return result;
}
Status verify_native_iostore(const std::string& name,const std::array<NativeIoStorePackageInput,4>& inputs,const Bytes& toc,const Bytes& cas) {
    auto expected=build_native_iostore(name,inputs); if(!expected.status.ok()) return expected.status;
    if(toc!=expected.utoc||cas!=expected.ucas) return bad("Native IoStore bytes/metadata differ from deterministic package projection");
    return Status::ok_status();
}
} // namespace ff7rp::pipeline
