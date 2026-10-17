#include "pipeline/native_iostore_writer.h"
#include "pipeline/native_asset_hash.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ff7rp::pipeline;
using namespace ff7rp::pipeline::native_binary;
namespace {
std::string hex(std::span<const std::uint8_t> b) { std::string s; for(auto c:b) { s+="0123456789abcdef"[c>>4]; s+="0123456789abcdef"[c&15]; } return s; }
int fail(const std::string& s) { std::cerr<<s<<'\n'; return 1; }
Bytes fixture(NativeAudioPackageRole role,std::size_t body_size) {
    const std::string objects[]{"Music_x","BGMList","PianoScoreInfo","PianoScore"};
    const auto object=objects[unsigned(role)];
    const auto package="/Game/NativeWriterTest/"+object;
    const auto cl=role==NativeAudioPackageRole::Music?std::string("/Script/SQEXSEAD.SQEXSEADMusic"):"/Script/EndDataObject.EndDataObject"+object;
    auto dot=cl.find('.'); auto def=cl.substr(0,dot)+".Default__"+cl.substr(dot+1);
    std::vector<std::string> names{package,"None",object};
    Bytes b(64); put(b,8,1); put(b,16,0x80802200); put(b,20,4096); put(b,24,64);
    for(auto& n:names) { b.push_back(std::uint8_t(n.size()>>8)); b.push_back(std::uint8_t(n.size())); b.insert(b.end(),n.begin(),n.end()); }
    put(b,28,b.size()-64); b.resize((b.size()+7)&~std::size_t(7)); put(b,32,b.size()); put(b,36,32); add(b,0xc1640000,8); for(auto& n:names) add(b,native_name_hash(n),8);
    put(b,40,b.size()); add(b,native_object_index(cl,1),8); add(b,native_object_index(def,1),8); add(b,native_object_index(cl.substr(0,dot),1),8);
    put(b,44,b.size()); add(b,0,8); add(b,body_size,8); add(b,2); add(b,0); add(b,~std::uint64_t(0),8); add(b,native_object_index(cl,1),8); add(b,~std::uint64_t(0),8); add(b,native_object_index(def,1),8); add(b,native_object_index(package+"."+object,2),8); add(b,11); add(b,0);
    put(b,48,b.size()); for(auto v:{0,1,1,1,0,0,0,1}) add(b,v); put(b,52,b.size()); put(b,56,4); add(b,0);
    for(std::size_t i=0;i<body_size;++i) b.push_back(std::uint8_t(i%251)); return b;
}
Bytes read(const std::filesystem::path& p) { std::ifstream f(p,std::ios::binary); return Bytes(std::istreambuf_iterator<char>(f),{}); }
bool write(const std::filesystem::path& p,const Bytes& b) { std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size())); return bool(f); }
}
int main(int argc,char** argv) {
    if(hex(native_blake3({}))!="af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262") return fail("BLAKE3 empty vector");
    Bytes abc{'a','b','c'};
    if(hex(native_blake3(abc))!="6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85") return fail("BLAKE3 abc vector");
    if(hex(native_sha256(abc))!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return fail("SHA256 abc vector");
    if(native_name_hash("/game/sound/bgm/bgm_piano_01",true)!=0xe1817fba8041bd80ULL) return fail("UE4 package CityHash vector");
    std::array<NativeIoStorePackageInput,4> inputs;
    for(unsigned i=0;i<4;++i) { inputs[i].role=NativeAudioPackageRole(i); inputs[i].zen=fixture(inputs[i].role,i==0?131137:257); }
    auto built=build_native_iostore("native_writer_test",inputs); if(!built.status.ok()) return fail(built.status.message);
    if(!verify_native_iostore("native_writer_test",inputs,built.utoc,built.ucas).ok()) return fail("Deterministic verify");
    auto again=build_native_iostore("native_writer_test",inputs); if(again.utoc!=built.utoc||again.ucas!=built.ucas) return fail("Determinism");
    // Independent minimal ContainerHeader decoder: final chunk starts at the
    // physical offset of its block, not its block-aligned virtual address.
    auto blocks=get(built.utoc,28); constexpr std::size_t block_at=144+5*22; auto start=std::size_t(get(built.utoc,std::size_t(block_at+(blocks-1)*12),5));
    auto h=std::span(built.ucas).subspan(start);
    if(get(h,0,8)!=built.container_id||get(h,8)!=4||get(h,12)!=0||get(h,16)!=0||get(h,20)!=8||get(h,24,8)!=0xc1640000||get(h,32)!=4||get(h,68)!=128) return fail("Initial ContainerHeader geometry");
    std::uint64_t previous=0;
    for(std::size_t i=0;i<4;++i) {
        auto id=get(h,36+i*8,8); if(id<=previous) return fail("Sorted StoreEntries package IDs"); previous=id;
        auto at=72+i*32;
        if(!get(h,at,8)||get(h,at+8)!=1||get(h,at+12)!=2||get(h,at+16)!=0||get(h,at+20)!=0xffffffff||get(h,at+24,8)!=0) return fail("Real Initial StoreEntry");
    }
    for(std::size_t at:{std::size_t(16),std::size_t(144),built.utoc.size()-1}) { auto damaged=built.utoc; damaged[at]^=1; if(verify_native_iostore("native_writer_test",inputs,damaged,built.ucas).ok()) return fail("Corrupt TOC accepted"); }
    auto damaged=built.ucas; damaged.back()^=1; if(verify_native_iostore("native_writer_test",inputs,built.utoc,damaged).ok()) return fail("Corrupt StoreEntry accepted");
    NativeZenPackage p; auto s=inspect_native_zen_package(inputs[3].zen,inputs[3].role,p); if(!s.ok()) return fail(s.message);
    auto names=p.names; const std::string package="/Game/NativeWriterTest/AnArbitrarilyLongerNamespace/PianoScore"; names.push_back(package); names.push_back("row_deadbeef");
    Bytes body(inputs[3].zen.begin()+p.body_at,inputs[3].zen.end()),renamed;
    s=rebuild_native_zen_package(inputs[3].zen,inputs[3].role,package,"PianoScore",names,body,renamed); if(!s.ok()) return fail(s.message);
    if(!inspect_native_zen_package(renamed,inputs[3].role,p).ok()||p.package_name!=package||!std::equal(body.begin(),body.end(),renamed.begin()+p.body_at)) return fail("Growing NameMap/body preservation");
    auto invalid=inputs; invalid[1].zen[0]^=1; if(build_native_iostore("native_writer_test",invalid).status.ok()) return fail("Wrong package identity accepted");
    invalid=inputs; invalid[1].role=NativeAudioPackageRole::Music; if(build_native_iostore("native_writer_test",invalid).status.ok()) return fail("Wrong role accepted");
    for(std::size_t n=0;n<inputs[1].zen.size();++n) { Bytes short_zen(inputs[1].zen.begin(),inputs[1].zen.begin()+n); if(inspect_native_zen_package(short_zen,inputs[1].role,p).ok()) return fail("Truncated Zen accepted"); }
    if(argc==3) {
        const std::string files[]{"music","bgmlist","scoreinfo","pianoscore"};
        for(unsigned i=0;i<4;++i) inputs[i].zen=read(std::filesystem::path(argv[1])/(files[i]+".zen.bin"));
        built=build_native_iostore("native_writer_test",inputs); if(!built.status.ok()) return fail("Real templates: "+built.status.message);
        if(!write(std::filesystem::path(argv[2]).replace_extension(".utoc"),built.utoc)||!write(std::filesystem::path(argv[2]).replace_extension(".ucas"),built.ucas)) return fail("Offline output write");
        std::cout<<"Real four-package writer output created (offline only)\n";
    } else if(argc!=1) return fail("Usage: native_iostore_writer_selftest [decoded-cohort-dir output-prefix]");
    std::cout<<"native_iostore_writer_selftest passed\n"; return 0;
}
