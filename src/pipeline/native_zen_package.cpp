#include "native_zen_package.h"
#include "native_asset_hash.h"
#include <algorithm>
#include <set>

namespace ff7rp::pipeline {
using namespace native_binary;
namespace {
Status bad(const char* s) { return Status::error(StatusCode::InvalidArgument,s); }
std::string mapped(const Bytes& b,std::size_t at,const std::vector<std::string>& names) {
    auto i=get(b,at), n=get(b,at+4); if(i>=names.size() || n>0x7fffffff) return {};
    return names[i]+(n?"_"+std::to_string(n-1):"");
}
const char* cls(NativeAudioPackageRole r) {
    switch(r) {
    case NativeAudioPackageRole::Music: return "/Script/SQEXSEAD.SQEXSEADMusic";
    case NativeAudioPackageRole::BgmList: return "/Script/EndDataObject.EndDataObjectBGMList";
    case NativeAudioPackageRole::ScoreInfo: return "/Script/EndDataObject.EndDataObjectPianoScoreInfo";
    case NativeAudioPackageRole::PianoScore: return "/Script/EndDataObject.EndDataObjectPianoScore";
    } return "";
}
}
std::uint64_t native_object_index(std::string path,unsigned type) {
    for(auto& c:path) if(c=='.'||c==':') c='/';
    return (native_name_hash(path,true)&0x3fffffffffffffffULL)|(std::uint64_t(type)<<62);
}
Status inspect_native_zen_package(const Bytes& b,NativeAudioPackageRole role,NativeZenPackage& output) {
    output={};
    if(b.size()<64 || b.size()>64*1024*1024 || get(b,24)!=64 || get(b,60)!=0) return bad("Unsupported native Zen summary");
    auto size=get(b,28),hash=get(b,32),hash_size=get(b,36),imp=get(b,40),exp=get(b,44),bundles=get(b,48),graph=get(b,52),gs=get(b,56);
    if(!range(b,64,size) || hash!=((64+size+7)&~std::uint64_t(7)) || hash_size<16 || hash_size>65536 ||
       !range(b,hash,hash_size) || imp!=hash+hash_size || imp>exp || exp-imp!=24 || bundles!=exp+72 ||
       !range(b,exp,72) || graph<bundles || (graph-bundles!=24 && graph-bundles!=32) || (gs!=4 && gs!=8 && gs!=16) ||
       !range(b,graph,gs) || graph+gs>=b.size()) return bad("Unsupported native Zen spans/imports/graph");
    for(auto at=graph;at<graph+gs;++at) if(b[std::size_t(at)]) return bad("Native Zen external package arcs/padding unsupported");
    NativeZenPackage p; p.role=role; p.class_path=cls(role); p.import_at=std::size_t(imp); p.export_at=std::size_t(exp); p.bundle_at=std::size_t(bundles); p.body_at=std::size_t(graph+gs);
    for(std::size_t at=64;at<64+size;) {
        if(64+size-at<2) return bad("Truncated Zen name");
        auto n=(unsigned(b[at])<<8)|b[at+1]; at+=2;
        if(n==0 || n>32767 || n>64+size-at || p.names.size()>=8192) return bad("Unsupported Zen name encoding/length");
        std::string s(b.begin()+at,b.begin()+at+n); if(!ascii(s)) return bad("Non-ASCII Zen template name");
        p.names.push_back(s); at+=n;
    }
    if(std::set<std::string>(p.names.begin(),p.names.end()).size()!=p.names.size() || hash_size!=8*(p.names.size()+1) || get(b,std::size_t(hash),8)!=0xc1640000) return bad("Zen NameMap hash geometry");
    for(std::size_t i=0;i<p.names.size();++i) if(get(b,std::size_t(hash)+8+i*8,8)!=native_name_hash(p.names[i])) return bad("Zen name hash mismatch");
    for(auto at=64+size;at<hash;++at) if(b[std::size_t(at)]) return bad("Zen NameMap padding");
    p.package_name=mapped(b,0,p.names); p.export_name=mapped(b,std::size_t(exp)+16,p.names);
    const auto source_name=mapped(b,8,p.names);
    if(p.package_name.rfind("/Game/",0)!=0 || p.package_name.find("..")!=std::string::npos || p.package_name.find('.')!=std::string::npos ||
       (source_name!="None" && source_name!=p.package_name) || p.export_name.empty() || p.export_name.find('/')!=std::string::npos) return bad("Zen package/object identity");
    auto cl=native_object_index(p.class_path,1);
    auto dot=p.class_path.find('.'); auto def=native_object_index(p.class_path.substr(0,dot)+".Default__"+p.class_path.substr(dot+1),1);
    if(get(b,std::size_t(exp)+24,8)!=~std::uint64_t(0) || get(b,std::size_t(exp)+32,8)!=cl || get(b,std::size_t(exp)+40,8)!=~std::uint64_t(0) ||
       get(b,std::size_t(exp)+48,8)!=def || get(b,std::size_t(exp)+64)!=11 || get(b,std::size_t(exp)+68)!=0 ||
       get(b,std::size_t(exp)+8,8)!=b.size()-p.body_at) return bad("Zen class/template/flags/export extent");
    std::set<std::uint64_t> imports;
    for(auto at=imp;at<exp;at+=8) { auto v=get(b,std::size_t(at),8); if(v>>62!=1 || !imports.insert(v).second) return bad("Zen non-script/duplicate import"); }
    if(!imports.contains(cl)||!imports.contains(def)) return bad("Zen missing class/template imports");
    p.public_export_hash=native_object_index(p.package_name+"."+p.export_name,2);
    if(get(b,std::size_t(exp)+56,8)!=p.public_export_hash) return bad("Zen public export hash");
    // Qualified stock: one bundle with two commands; converter: two one-command bundles.
    const std::vector<std::uint32_t> expected=graph-bundles==32?std::vector<std::uint32_t>{0,1,1,1,0,0,0,1}:std::vector<std::uint32_t>{0,2,0,0,0,1};
    for(std::size_t i=0;i<expected.size();++i) if(get(b,std::size_t(bundles)+i*4)!=expected[i]) return bad("Zen export bundle command layout");
    p.export_bundle_count=graph-bundles==32?2:1; p.package_id=native_name_hash(p.package_name,true); output=std::move(p); return Status::ok_status();
}
Status rebuild_native_zen_package(const Bytes& source,NativeAudioPackageRole role,const std::string& package,const std::string& object,
    const std::vector<std::string>& names,const Bytes& body,Bytes& output) {
    NativeZenPackage p; auto s=inspect_native_zen_package(source,role,p); if(!s.ok()) { output.clear(); return s; }
    if(names.size()<p.names.size() || names.size()>8192 || body.empty() || body.size()>64*1024*1024 ||
       std::set<std::string>(names.begin(),names.end()).size()!=names.size()) { output.clear(); return bad("Zen rebuild names/body"); }
    auto pi=std::find(names.begin(),names.end(),package),oi=std::find(names.begin(),names.end(),object),ni=std::find(names.begin(),names.end(),"None");
    if(pi==names.end()||oi==names.end()||ni==names.end()) { output.clear(); return bad("Zen rebuild identities missing"); }
    Bytes b(source.begin(),source.begin()+64); put(b,0,pi-names.begin()); put(b,4,0); put(b,8,ni-names.begin()); put(b,12,0);
    for(const auto& n:names) {
        if(!ascii(n)||n.size()>32767) { output.clear(); return bad("Zen rebuild name encoding"); }
        b.push_back(std::uint8_t(n.size()>>8)); b.push_back(std::uint8_t(n.size())); b.insert(b.end(),n.begin(),n.end());
    }
    put(b,28,b.size()-64); b.resize((b.size()+7)&~std::size_t(7)); put(b,32,b.size()); put(b,36,8*(names.size()+1)); add(b,0xc1640000,8);
    for(const auto& n:names) add(b,native_name_hash(n),8);
    put(b,40,b.size()); append(b,std::span(source).subspan(p.import_at,24)); put(b,44,b.size()); auto e=b.size(); append(b,std::span(source).subspan(p.export_at,72));
    put(b,e,0,8); put(b,e+8,body.size(),8); put(b,e+16,oi-names.begin()); put(b,e+20,0); put(b,e+56,native_object_index(package+"."+object,2),8);
    put(b,48,b.size()); for(auto v:{0,1,1,1,0,0,0,1}) add(b,v); put(b,52,b.size()); put(b,56,4); add(b,0); append(b,body);
    NativeZenPackage checked; s=inspect_native_zen_package(b,role,checked); if(!s.ok()) { output.clear(); return s; }
    output=std::move(b); return Status::ok_status();
}
} // namespace ff7rp::pipeline
