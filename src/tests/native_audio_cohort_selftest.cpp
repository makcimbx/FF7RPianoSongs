#include "pipeline/native_audio_cohort.h"
#include "pipeline/native_asset_hash.h"
#include "pipeline/mabf_builder.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>

using namespace ff7rp::pipeline;
using namespace ff7rp::pipeline::native_binary;
namespace {
int fail(const std::string& s) { std::cerr<<s<<'\n'; return 1; }
Bytes read(const std::filesystem::path& path) { std::ifstream f(path,std::ios::binary); return Bytes(std::istreambuf_iterator<char>(f),{}); }
std::string hex(std::span<const std::uint8_t> b) { std::string s; for(auto c:b) { s+="0123456789abcdef"[c>>4]; s+="0123456789abcdef"[c&15]; } return s; }
bool preserved_table(const Bytes& source,const Bytes& output,NativeAudioPackageRole role,
    const std::string& key,const std::string& resource) {
    NativeZenPackage s,p;
    if(!inspect_native_zen_package(source,role,s).ok()||!inspect_native_zen_package(output,role,p).ok()) return false;
    using Fixups=std::map<std::size_t,std::string>;
    auto decode=[](const Bytes& b,const NativeZenPackage& z,Bytes& content,Fixups& fixups) {
        auto size=get(b,z.body_at+6); content.assign(b.begin()+z.body_at+32,b.begin()+z.body_at+32+size);
        auto at=z.body_at+32+size; auto groups=get(b,at+8); at+=12;
        for(std::size_t g=0;g<groups;++g) {
            auto ni=get(b,at),number=get(b,at+4),count=get(b,at+8);at+=12;
            auto name=z.names[ni]+(number?"_"+std::to_string(number-1):"");
            for(std::size_t j=0;j<count;++j) {fixups[std::size_t(get(b,at))]=name;at+=4;}
        }
        return at;
    };
    Bytes before,after; Fixups old_names,new_names;
    auto old_tail=decode(source,s,before,old_names),new_tail=decode(output,p,after,new_names);
    if(before.size()!=after.size()||!std::equal(source.begin()+s.body_at,source.begin()+s.body_at+32,output.begin()+p.body_at)||
       Bytes(source.begin()+old_tail,source.end())!=Bytes(output.begin()+new_tail,output.end())) return false;
    auto ptr=[](const Bytes& b,std::size_t h){return std::size_t(std::int64_t(h)+(std::int64_t(get(b,h,8))>>1));};
    auto key_at=ptr(after,0),bits=ptr(after,16),hash=ptr(after,40),row=ptr(after,72),old_row=ptr(before,72);
    const std::size_t stride=role==NativeAudioPackageRole::BgmList?132:role==NativeAudioPackageRole::ScoreInfo?120:176;
    if(row<old_row||(row-old_row)%stride||(row-old_row)/stride>=get(before,80)||
       get(after,8)!=1||get(after,12)!=1||get(after,24)!=1||get(after,48)!=1||get(after,80)!=1||get(after,84)!=1||
       get(after,key_at+8)!=0||get(after,key_at+12)!=0xffffffff||get(after,key_at+16)!=0||get(after,bits)!=1||get(after,hash)!=0) return false;
    old_names[key_at]=key;
    if(!resource.empty()) old_names[row+(role==NativeAudioPackageRole::BgmList?96:80)]=resource;
    if(old_names!=new_names) return false;
    // Only map bookkeeping changes in content; selected and unreachable source
    // rows, property tables and all relative payloads remain byte-identical.
    for(std::size_t i=0;i<before.size();++i) {
        const bool allowed=i<32||(i>=40&&i<56)||(i>=72&&i<88)||
            (i>=key_at&&i<key_at+20)||(i>=bits&&i<bits+4)||(i>=hash&&i<hash+4);
        if(!allowed&&before[i]!=after[i]) return false;
    }
    return true;
}
}
int main(int argc,char** argv) {
    for(const auto& id:{std::string("Song ")+"\xe6\x9b\xb2",std::string(1024,'x')})
        if(!valid_native_audio_identity(id))return fail("Opaque repository identity rejected");
    for(const auto& id:{std::string{},std::string(1025,'x'),std::string("bad\n"),std::string("bad\0id",6)})
        if(valid_native_audio_identity(id))return fail("Invalid repository identity accepted");
    NativeAudioCohortRequest r;
    if(build_native_audio_cohort(r).status.ok()) return fail("Empty request accepted");
    r.build_id="ff7rebirth-steam-win64-6a16ced2"; r.identity="cohort-selftest"; r.base_score_name="Journey";
    if(build_native_audio_cohort(r).status.ok()) return fail("Missing provenance accepted");
    if(argc==7) {
        for(std::size_t i=0;i<4;++i) r.source_zen[i]=read(argv[i+1]); r.mabf=read(argv[5]);
        MabfArtifactMetadata timing; auto ts=validate_structural_mabf(r.mabf,&timing); if(!ts.ok()) return fail(ts.message);
        r.timing={timing.logical_source_frames,timing.sample_rate,double(timing.logical_source_frames)/timing.sample_rate};
        auto root=std::filesystem::absolute(argv[6]);
        auto built=build_native_audio_cohort(r); if(!built.status.ok()) return fail(built.status.message);
        auto again=build_native_audio_cohort(r); if(!again.status.ok()||again.container.utoc!=built.container.utoc||again.container.ucas!=built.container.ucas) return fail("Cohort determinism");
        const char* objects[]{"","BGMList","PianoScoreInfo","PianoScore"};
        NativeZenPackage original_music; if(!inspect_native_zen_package(r.source_zen[0],NativeAudioPackageRole::Music,original_music).ok()) return fail("Source Music inspection");
        Bytes original_body(r.source_zen[0].begin()+original_music.body_at,r.source_zen[0].end());
        auto expected_music=build_native_music_export(original_body,r.mabf,r.timing);
        // Explicit current-donor conversion and synthetic older source profiles;
        // these fixtures are not a claim of independently hashed older stock.
        auto older=r; older.build_id="ff7rebirth-steam-win64-68fd6fde";
        auto valid_old=[&](const NativeAudioCohortResult& b) {
            if(!b.status.ok()||b.content_id==built.content_id) return false;
            NativeZenPackage p;const auto& z=b.packages[0].zen;
            if(!inspect_native_zen_package(z,NativeAudioPackageRole::Music,p).ok()||get(z,p.export_at,8)||
               get(z,p.export_at+8,8)!=160+r.mabf.size()-48+40||get(z,p.body_at+0x58,8)!=3||
               p.names[3]!=b.music_name||p.package_name!="/Game/Sound/BGM/"+b.music_name||
               (get(z,16)&0x80802200)!=0x80802200||z[p.body_at+0x69]||z[p.body_at+0x6a]!=4||get(z,p.body_at+0x6f)) return false;
            // Independently locate Initial PackageStore and inspect its packed
            // sizes: high nibble0 means native byte alignment, NOT16.
            const auto& toc=b.container.utoc;const auto& cas=b.container.ucas;
            auto be5=[&](std::size_t at){std::uint64_t v=0;for(int i=0;i<5;++i)v=(v<<8)|toc[at+i];return v;};
            auto offset=be5(144+5*12+4*10),length=be5(144+5*12+4*10+5);
            auto disk=get(toc,144+5*22+(offset/65536)*12,5);std::span<const std::uint8_t> h(cas.data()+disk,std::size_t(length));
            std::size_t at=16;at+=4+std::size_t(get(h,at));at+=4+std::size_t(get(h,at));
            if(get(h,at)!=4) return false;at+=4;const auto ids=at;at+=32;
            if(get(h,at)!=128) return false;at+=4;
            for(std::size_t i=0;i<4;++i) {
                const auto id=get(h,ids+i*8,8),word=get(h,at+i*32,8);
                bool found=false;for(std::size_t j=0;j<4;++j) if(id==b.container.packages[j].package_id) found=word==b.packages[j].zen.size();
                if(!found||(word>>60)||get(h,at+i*32+8)!=1||get(h,at+i*32+12)!=2) return false;
            }
            return true;
        };
        auto old_donor=build_native_audio_cohort(older);
        if(!valid_old(old_donor)) return fail("Qualified older donor projection: "+old_donor.status.message);
        auto old_body=original_body; old_body[5]=4;
        old_body.erase(old_body.begin()+0x63); old_body.insert(old_body.begin()+0x7f,0); put(old_body,0x79,5,2);
        auto os=rebuild_native_zen_package(r.source_zen[0],NativeAudioPackageRole::Music,original_music.package_name,
            original_music.export_name,original_music.names,old_body,older.source_zen[0]);
        auto old_native=build_native_audio_cohort(older);
        if(!os.ok()||!valid_old(old_native)||old_native.content_id==old_donor.content_id) return fail("Explicit older reflected fixture validation/identity");
        auto source40=older;auto body40=old_body;body40.resize(body40.size()-8);body40[body40.size()-20]^=0x20;
        if(!rebuild_native_zen_package(r.source_zen[0],NativeAudioPackageRole::Music,original_music.package_name,original_music.export_name,
            original_music.names,body40,source40.source_zen[0]).ok()) return fail("Older suffix40 source fixture");
        auto projected40=build_native_audio_cohort(source40);
        if(!valid_old(projected40)||projected40.content_id==old_native.content_id) return fail("Actual source bytes/GUID must change older cache identity");
        // Independently repack the old compact NameMap (no SW2 entry). This is
        // test fixture construction, not a second shipping Zen serializer.
        auto compact=source40;auto compact_names=original_music.names;compact_names.erase(compact_names.begin()+7);
        auto compact_body=body40;for(auto off:{0x26,0x32,0x3e}) put(compact_body,off,get(compact_body,off)-1);
        Bytes compact_zen(r.source_zen[0].begin(),r.source_zen[0].begin()+64);
        for(const auto& n:compact_names) {add(compact_zen,n.size()>>8,1);add(compact_zen,n.size()&255,1);compact_zen.insert(compact_zen.end(),n.begin(),n.end());}
        put(compact_zen,28,compact_zen.size()-64);compact_zen.resize((compact_zen.size()+7)&~std::size_t(7));
        put(compact_zen,32,compact_zen.size());put(compact_zen,36,8*(compact_names.size()+1));add(compact_zen,0xc1640000,8);
        for(const auto& n:compact_names) add(compact_zen,native_name_hash(n),8);
        auto new_import=compact_zen.size();put(compact_zen,40,new_import);
        for(auto field:{44,48,52}) put(compact_zen,field,get(r.source_zen[0],field)-original_music.import_at+new_import);
        compact_zen.insert(compact_zen.end(),r.source_zen[0].begin()+original_music.import_at,r.source_zen[0].begin()+original_music.body_at);
        put(compact_zen,std::size_t(get(compact_zen,44))+8,compact_body.size(),8);append(compact_zen,compact_body);compact.source_zen[0]=std::move(compact_zen);
        if(!valid_old(build_native_audio_cohort(compact))) return fail("Compact older NameMap/cue source coherence");
        NativeAudioArtifactSet old_set;auto old_root=root/"older-profile";
        if(!prepare_native_audio_cohort(older,old_root,old_set).ok()||!verify_native_audio_cohort(older,old_set).ok()) return fail("Older immutable cohort publication");
        const auto old_mtime=std::filesystem::last_write_time(old_set.files[2].path);NativeAudioArtifactSet old_reused;
        if(!prepare_native_audio_cohort(older,old_root,old_reused).ok()||old_mtime!=std::filesystem::last_write_time(old_set.files[2].path)) return fail("Older immutable reuse");
        auto old_forged=old_set;old_forged.build_id=r.build_id;
        if(verify_native_audio_cohort(older,old_forged).ok()||verify_native_audio_cohort(r,old_set).ok()) return fail("Cross-build artifact admitted");
        old_forged=old_set;old_forged.music_name="bgm_piano_01";
        if(verify_native_audio_cohort(older,old_forged).ok()) return fail("Forged older cue accepted");
        for(std::size_t i=0;i<4;++i) {
            if(old_set.packages[i].package_id==built.container.packages[i].package_id) return fail("Build-specific native package identity collision");
            std::filesystem::create_directories(root);std::ofstream out(root/("older-generated_"+std::to_string(i)+".zen.bin"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(old_native.packages[i].zen.data()),std::streamsize(old_native.packages[i].zen.size()));
            if(i&&!preserved_table(older.source_zen[i],old_native.packages[i].zen,NativeAudioPackageRole(i),i==1?old_native.bgm_key:old_native.score_name,i==1?old_native.music_name:i==2?old_native.bgm_key:"")) return fail("Older row/fixup/carrier preservation");
        }
        std::cout<<"Offline older-profile cohort prefix: "<<old_set.container_prefix.string()<<'\n';
        auto rejected_before_archive=[&](const NativeAudioCohortRequest& input) {
            auto b=build_native_audio_cohort(input);NativeAudioArtifactSet refused;auto path=root/"malformed-old-must-not-publish";
            return !b.status.ok()&&b.container.ucas.empty()&&!prepare_native_audio_cohort(input,path,refused).ok()&&refused.identity.empty()&&!std::filesystem::exists(path);
        };
        auto malformed=older; malformed.source_zen[0].back()=1;
        if(!rejected_before_archive(malformed)) return fail("Unknown older cooked suffix accepted");
        malformed=r;malformed.build_id=older.build_id;malformed.source_zen[0].back()^=1;
        if(!rejected_before_archive(malformed)) return fail("Failed current donor SHA fell back to older semantic acceptance");
        for(std::size_t role=1;role<4;++role) {
            NativeZenPackage p; if(!inspect_native_zen_package(older.source_zen[role],NativeAudioPackageRole(role),p).ok()) return fail("Old fixture table inspection");
            const auto content=p.body_at+32;
            const auto props=content+56+(get(older.source_zen[role],content+56,8)>>1);
            malformed=older; put(malformed.source_zen[role],props+8,9);
            if(!rejected_before_archive(malformed)) return fail("Older same-stride wrong member type accepted");
            malformed=older; const auto fewer=get(malformed.source_zen[role],content+64)-1;
            put(malformed.source_zen[role],content+64,fewer);put(malformed.source_zen[role],content+68,fewer);
            if(!rejected_before_archive(malformed)) return fail("Older missing mandatory member accepted");
            const char* first[]{"SectionName00","ScoreStoryFlag_Array","TimeStr_Array"};
            const char* second[]{"ModeBGM","ScoreStateTrigger_Array","IgnoreSound_Array"};
            auto names=p.names; auto a=std::find(names.begin(),names.end(),first[role-1]),b=std::find(names.begin(),names.end(),second[role-1]);
            if(a==names.end()||b==names.end()) return fail("Ordered-member fixture names"); std::iter_swap(a,b);
            Bytes body(older.source_zen[role].begin()+p.body_at,older.source_zen[role].end()); malformed=older;
            if(!rebuild_native_zen_package(older.source_zen[role],NativeAudioPackageRole(role),p.package_name,p.export_name,names,body,malformed.source_zen[role]).ok()||
               !rejected_before_archive(malformed)) return fail("Older same-layout reordered mandatory members accepted");
            if(role>1) {
                const auto row=content+72+(get(older.source_zen[role],content+72,8)>>1);
                malformed=older; put(malformed.source_zen[role],row+8,0x7fffffff);put(malformed.source_zen[role],row+12,0x7fffffff);
                if(!rejected_before_archive(malformed)) return fail("Older malformed nested array accepted");
            }
        }
        for(std::size_t i=0;i<4;++i) {
            NativeZenPackage p; auto s=inspect_native_zen_package(built.packages[i].zen,NativeAudioPackageRole(i),p);
            if(!s.ok() || (i&&p.export_name!=objects[i]) || p.package_name.find(built.content_id)==std::string::npos) return fail("Unique package/canonical export identity");
            if(!i) {
                const auto& z=built.packages[i].zen;
                if(built.music_name.rfind("bgm_",0)!=0||built.music_name==built.bgm_key||p.package_name!="/Game/Sound/BGM/"+built.music_name||
                   p.export_name!=built.music_name||p.names.size()!=original_music.names.size()||p.names[0]!=p.package_name||p.names[3]!=built.music_name||
                   get(z,0,8)!=0||get(z,p.export_at+16,8)!=3||get(z,p.body_at+0x52,8)!=3||p.names[get(z,p.body_at+0x52)]!=p.export_name||
                   !expected_music.status.ok()||Bytes(z.begin()+p.body_at,z.end())!=expected_music.bytes||
                   !std::equal(r.source_zen[0].begin()+original_music.import_at,r.source_zen[0].begin()+original_music.export_at,z.begin()+p.import_at)) return fail("Canonical Music loading path/cue/body/import projection");
                for(std::size_t n=0;n<p.names.size();++n) if(n!=0&&n!=3&&p.names[n]!=original_music.names[n]) return fail("Unrelated Music name retargeted");
            } else if(!preserved_table(r.source_zen[i],built.packages[i].zen,NativeAudioPackageRole(i),i==1?built.bgm_key:built.score_name,i==1?built.music_name:i==2?built.bgm_key:"")) return fail("Table content/fixup/resource preservation");
            // Only test tools write these inspection copies; cache API writes only the trio.
            std::filesystem::create_directories(root); std::ofstream out(root/(std::string("generated_")+std::to_string(i)+".zen.bin"),std::ios::binary); const auto& b=built.packages[i].zen; out.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
        }
        NativeAudioArtifactSet set; auto s=prepare_native_audio_cohort(r,root,set); if(!s.ok()) return fail(s.message);
        if(set.identity!=r.identity||set.base_score_name!=r.base_score_name||hex(native_sha256(read(set.files[0].path)))!="75e7144577253917f6da7312ef5e585b12fb728226a22b0938323751a6b555cd") return fail("Song identity/source carrier/empty v11 pak compatibility");
        if(!verify_native_audio_cohort(r,set).ok()) return fail("Cache verify");
        auto before=std::filesystem::last_write_time(set.files[2].path); NativeAudioArtifactSet reused; s=prepare_native_audio_cohort(r,root,reused);
        if(!s.ok()||before!=std::filesystem::last_write_time(set.files[2].path)||set.identity!=reused.identity) return fail("Immutable cache reuse");
        auto changed=r; changed.identity+="-changed"; auto other=build_native_audio_cohort(changed);
        if(!other.status.ok()||other.content_id==built.content_id||other.container.container_id==built.container.container_id) return fail("Distinct song content identity");
        for(std::size_t i=0;i<4;++i) if(other.container.packages[i].package_id==built.container.packages[i].package_id) return fail("Retained package ID reused");
        auto opaque=r;opaque.identity="Song \xe6\x9b\xb2";
        auto opaque_cohort=build_native_audio_cohort(opaque);
        if(!opaque_cohort.status.ok()||opaque_cohort.content_id==built.content_id)return fail("Opaque song identity projection");
        const char* carriers[]{"Journey","TifasTheme","BarretTheme","SyncoDeChocobo","DifficultToStandOnTwoLegs","AerithsTheme","Fighters","OneWingedAngel"};
        for(auto carrier:carriers) { auto request=r; request.base_score_name=carrier; auto projection=build_native_audio_cohort(request); if(!projection.status.ok()) return fail(std::string(carrier)+": "+projection.status.message); }
        NativeZenPackage music; if(!inspect_native_zen_package(r.source_zen[0],NativeAudioPackageRole::Music,music).ok()) return fail("Source Music inspection");
        auto changed_bank=r; changed_bank.mabf=Bytes(r.source_zen[0].begin()+music.body_at+144,r.source_zen[0].end());
        auto bank_projection=build_native_audio_cohort(changed_bank); if(!bank_projection.status.ok()||bank_projection.content_id==built.content_id) return fail("Changed valid bank must receive fresh identity");
        for(std::size_t i=0;i<4;++i) { auto invalid=r; invalid.source_zen[i].back()^=1; if(build_native_audio_cohort(invalid).status.ok()) return fail("Template provenance mutation accepted"); }
        auto invalid=r; invalid.mabf.pop_back(); if(build_native_audio_cohort(invalid).status.ok()) return fail("Unequal Music geometry accepted");
        invalid=r; invalid.base_score_name="NoSuchStockRow"; if(build_native_audio_cohort(invalid).status.ok()) return fail("Missing carrier fallback");
        invalid=r; invalid.build_id="unknown"; if(build_native_audio_cohort(invalid).status.ok()) return fail("Unknown build accepted");
        auto forged=set; forged.packages[2].public_export_hash^=1; if(verify_native_audio_cohort(r,forged).ok()) return fail("Forged package descriptor accepted");
        forged=set; forged.packages[0].object_path="/Game/FF7RPianoSongs/"+built.content_id+"/"+built.music_name+"."+built.music_name;
        if(verify_native_audio_cohort(r,forged).ok()) return fail("Forged private Music loading path accepted");
        forged=set; forged.music_name="bgm_piano_01"; if(verify_native_audio_cohort(r,forged).ok()) return fail("Forged stock cue accepted");
        forged=set; forged.bgm_key=forged.music_name; if(verify_native_audio_cohort(r,forged).ok()) return fail("Aliased cue/BGM row key accepted");
        forged=set; forged.base_score_name="UnverifiedCarrier"; if(verify_native_audio_cohort(r,forged).ok()) return fail("Forged source carrier accepted");
        forged=set; forged.files[0].path=forged.files[1].path; if(verify_native_audio_cohort(r,forged).ok()) return fail("Forged file path accepted");
        // Mutating a separate content address must never rewrite the first cohort.
        NativeAudioArtifactSet corrupt; s=prepare_native_audio_cohort(changed,root,corrupt); if(!s.ok()) return fail(s.message);
        NativeZenPackage corrupt_music; if(!inspect_native_zen_package(other.packages[0].zen,NativeAudioPackageRole::Music,corrupt_music).ok()) return fail("Corrupt-test Music inspection");
        auto corrupt_bytes=read(corrupt.files[2].path);
        // Music is the first stored chunk: forge its inner cue FName, leaving
        // package identity intact. Verification must refuse, never repair it.
        corrupt_bytes[corrupt_music.body_at+0x52]^=1;
        { std::ofstream out(corrupt.files[2].path,std::ios::binary); out.write(reinterpret_cast<const char*>(corrupt_bytes.data()),std::streamsize(corrupt_bytes.size())); }
        NativeAudioArtifactSet refused; if(prepare_native_audio_cohort(changed,root,refused).ok()||!refused.identity.empty()||read(corrupt.files[2].path)!=corrupt_bytes) return fail("Corrupt immutable cache rewritten/published");
        if(!verify_native_audio_cohort(r,set).ok()) return fail("Unrelated retained cohort changed");
        std::filesystem::remove_all(corrupt.container_prefix.parent_path()); // Test-only, never mounted.
        std::cout<<"Offline cohort prefix: "<<set.container_prefix.string()<<'\n';
    } else if(argc!=1) return fail("Usage: native_audio_cohort_selftest [stock-music stock-bgmlist stock-scoreinfo stock-pianoscore mabf cache-root]");
    std::cout<<"native_audio_cohort_selftest passed\n"; return 0;
}
