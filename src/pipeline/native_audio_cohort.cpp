#include "native_audio_cohort.h"
#include "native_asset_hash.h"
#include "native_music_export.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <bit>
#include <cmath>

namespace ff7rp::pipeline {
using namespace native_binary;
namespace {
constexpr const char* kCurrentBuild="ff7rebirth-steam-win64-6a16ced2";
constexpr const char* kOlderBuild="ff7rebirth-steam-win64-68fd6fde";
Status bad(const char* s) { return Status::error(StatusCode::InvalidArgument,s); }
std::string hex(std::span<const std::uint8_t> b) { std::string s; for(auto c:b) { s+="0123456789abcdef"[c>>4]; s+="0123456789abcdef"[c&15]; } return s; }
struct Frozen {
    Bytes prefix,content,tail;
    std::map<std::size_t,std::pair<std::uint32_t,std::uint32_t>> fixes;
    std::array<std::size_t,5> starts{}, counts{};
    std::vector<std::string> keys;
    std::vector<std::size_t> key_rows;
    std::map<std::string,std::size_t> properties;
    std::size_t stride=0;
};
struct Member { const char* name; unsigned tag; std::size_t offset; };
constexpr Member kBgmMembers[]{
    {"SectionName00",11,0},{"ModeBGM",11,8},{"SkipSectionName",11,16},{"RestartSectionName",11,24},
    {"SectionName02",11,32},{"SectionName03",11,40},{"SectionName04",11,48},{"SectionName05",11,56},
    {"SectionName06",11,64},{"SectionName07",11,72},{"FinishSectionName",11,80},{"BattleBGMName",11,88},
    {"BGMName",11,96},{"UniqueIndex",7,104},{"FadeOutTime",9,108},{"OverrideFadeOutTime",9,112},
    {"OneShotPlayTime",9,116},{"FadeInTime",9,120},{"IsHighPriorityLoad",2,124},{"CrossFade",2,125},
    {"OneShot",2,126},{"IgnoreVehicleRequest",2,127},{"EnablePrevFinishSection",2,128},{"PauseRestart",2,129}};
constexpr Member kInfoMembers[]{
    {"ScoreStoryFlag_Array",11,0},{"ScoreStateTrigger_Array",11,16},{"ModeChangeComboCount_Array",6,32},
    {"Score_Array",7,48},{"MenuTextID",10,64},{"BGMName",11,80},{"CameraPlayMinTime",9,88},
    {"CameraPlayMaxTime",9,92},{"CameraSwitchRate",9,96},{"Bpm",9,100},{"StreamingFrame",6,104},
    {"Difficulty",7,108},{"DefaultSpeedIndex",2,112},{"MinSpeedIndex",2,113},{"UniqueIndex",2,114}};
constexpr Member kScoreMembers[]{
    {"TimeStr_Array",10,0},{"IgnoreSound_Array",11,16},{"MonotoneID_Array",11,32},{"ChordID_Array",11,48},
    {"ChordNoteType_Array",2,64},{"ChordDotType_Array",2,80},{"MonotoneNoteType_Array",2,96},
    {"MonotoneDotType_Array",2,112},{"Strength_Array",2,128},{"CameraSwitchTiming_Array",2,144},{"GroupIndex_Array",2,160}};
Status frozen(const Bytes& zen,const NativeZenPackage& p,Frozen& f,bool validate_payloads=false) {
    auto body=std::span(zen).subspan(p.body_at);
    if(body.size()<44 || get(body,0,4)!=0x100 || get(body,4,2)!=0 || get(body,10)!=0x10000 || get(body,14,2)!=16 || get(body,16,2)!=14) return bad("Unqualified DataObject frozen wrapper");
    for(std::size_t i=18;i<32;++i) if(body[i]) return bad("Frozen wrapper padding");
    auto size=get(body,6); if(size<88 || !range(body,32,size+12)) return bad("Frozen content extent");
    f.prefix=Bytes(body.begin(),body.begin()+32); f.content=Bytes(body.begin()+32,body.begin()+32+size);
    auto at=std::size_t(32+size); if(get(body,at)||get(body,at+4)) return bad("Frozen vtable/script fixups unsupported"); auto groups=get(body,at+8); at+=12;
    if(groups==0||groups>8192) return bad("Frozen name fixup groups");
    for(std::size_t g=0;g<groups;++g) {
        if(!range(body,at,12)) return bad("Truncated frozen fixup");
        auto index=get(body,at),number=get(body,at+4),count=get(body,at+8); at+=12;
        if(index>=p.names.size()||number>0x7fffffff||!count||count>size/8||!range(body,at,count*4)) return bad("Frozen fixup identity/count");
        for(std::size_t j=0;j<count;++j) {
            auto offset=std::size_t(get(body,at)); at+=4;
            if(offset%4 || !range(f.content,offset,8)||get(f.content,offset,8)||!f.fixes.emplace(offset,std::pair(std::uint32_t(index),std::uint32_t(number))).second) return bad("Frozen fixup bounds/placeholder/duplicate");
        }
    }
    const std::size_t tail_size=p.role==NativeAudioPackageRole::ScoreInfo?12:p.role==NativeAudioPackageRole::PianoScore?8:0;
    if(body.size()-at!=tail_size) return bad("Frozen archive tail");
    f.tail=Bytes(body.begin()+at,body.end()); if(std::any_of(f.tail.begin(),f.tail.end(),[](auto c){return c!=0;})) return bad("Frozen tail bytes");
    std::size_t prev=0; bool first=true;
    for(auto [offset,pair]:f.fixes) { (void)pair; if(!first && offset<prev+8) return bad("Overlapping frozen fixups"); first=false; prev=offset; }
    constexpr std::size_t headers[]{0,16,40,56,72}; constexpr std::size_t widths[]{20,0,4,12,0};
    for(std::size_t i=0;i<5;++i) {
        auto h=headers[i],raw=get(f.content,h,8),count=get(f.content,h+8),capacity=get(f.content,h+12);
        if(!(raw&1)||raw>>63||!count||count>16384||capacity!=(i==2?0:count)) return bad("Frozen array header");
        auto start=std::uint64_t(h)+(raw>>1),len=i==1?((count+31)/32)*4:count*widths[i];
        if(start<88||!range(f.content,start,len)||start%4) return bad("Frozen relative array bounds");
        f.starts[i]=std::size_t(start); f.counts[i]=std::size_t(count);
    }
    if(f.counts[0]!=f.counts[1]||f.counts[0]!=f.counts[4]||get(f.content,32)!=0xffffffff||get(f.content,36)!=0||(f.counts[2]&(f.counts[2]-1))) return bad("Frozen sparse map geometry");
    auto fname=[&](std::size_t off) -> std::string {
        auto fix=f.fixes.find(off); if(fix==f.fixes.end()) return {};
        auto [i,n]=fix->second; return p.names[i]+(n?"_"+std::to_string(n-1):"");
    };
    const std::span<const Member> members=p.role==NativeAudioPackageRole::BgmList?std::span<const Member>(kBgmMembers):
        p.role==NativeAudioPackageRole::ScoreInfo?std::span<const Member>(kInfoMembers):std::span<const Member>(kScoreMembers);
    if(f.counts[3]!=members.size()) return bad("Unsupported frozen ordered member count");
    for(std::size_t i=0;i<f.counts[3];++i) {
        auto pos=f.starts[3]+i*12; auto name=fname(pos); auto tag=get(f.content,pos+8);
        if(name.empty()||f.properties.contains(name)) return bad("Frozen property identity");
        const auto canonical=(p.role==NativeAudioPackageRole::ScoreInfo&&name=="BgmName")?"BGMName":name;
        if(canonical!=members[i].name||tag!=members[i].tag) return bad("Unsupported frozen ordered member name/type");
        std::size_t alignment=4,width=4;
        if(name.ends_with("_Array")||tag==10) { alignment=8; width=16; }
        else if(tag==2) { alignment=1; width=1; }
        else if(tag==11) width=8;
        else if(tag!=6&&tag!=7&&tag!=9) return bad("Frozen property tag");
        f.stride=(f.stride+alignment-1)&~(alignment-1);
        if(f.stride!=members[i].offset) return bad("Unsupported frozen member offset");
        f.properties.emplace(canonical,f.stride); f.stride+=width;
    }
    f.stride=(f.stride+7)&~std::size_t(7);
    // BGMList's observed row alignment is four, unlike the piano row arrays.
    if(p.role==NativeAudioPackageRole::BgmList) f.stride=132;
    const auto expected=p.role==NativeAudioPackageRole::ScoreInfo?120:p.role==NativeAudioPackageRole::PianoScore?176:132;
    if(f.stride!=std::size_t(expected)||!range(f.content,f.starts[4],f.stride*f.counts[4])) return bad("Frozen qualified row stride");
    std::vector<int> next; std::vector<std::size_t> bucket;
    std::set<std::string> unique; std::set<std::size_t> row_indices;
    for(std::size_t i=0;i<f.counts[0];++i) {
        auto key=f.starts[0]+i*20; auto name=fname(key); auto row=get(f.content,key+8); auto link=std::int32_t(get(f.content,key+12)); auto hash=get(f.content,key+16);
        if(name.empty()||!unique.insert(name).second||row>=f.counts[4]||!row_indices.insert(std::size_t(row)).second||link< -1||link>=std::int64_t(f.counts[0])||hash>=f.counts[2]||!(get(f.content,f.starts[1]+i/32*4)&(std::uint64_t(1)<<(i%32)))) return bad("Frozen map key/row/chain");
        f.keys.push_back(name); f.key_rows.push_back(std::size_t(row)); next.push_back(link); bucket.push_back(std::size_t(hash));
    }
    std::set<int> visited;
    for(std::size_t i=0;i<f.counts[2];++i) {
        int index=std::int32_t(get(f.content,f.starts[2]+i*4));
        while(index!=-1) { if(index<0||std::size_t(index)>=f.keys.size()||!visited.insert(index).second||bucket[index]!=i) return bad("Frozen hash chain"); index=next[index]; }
    }
    if(visited.size()!=f.keys.size()) return bad("Unreachable frozen map keys");
    // Projected maps deliberately retain unreachable source rows/fixups. Their
    // complete image is compared to the already validated source projection.
    if(!validate_payloads) return Status::ok_status();
    const std::array<std::size_t,5> root_bytes{f.counts[0]*20,((f.counts[1]+31)/32)*4,
        f.counts[2]*4,f.counts[3]*12,f.counts[4]*f.stride};
    std::size_t payload_begin=88;
    for(std::size_t i=0;i<5;++i) {
        payload_begin=std::max(payload_begin,f.starts[i]+root_bytes[i]);
        for(std::size_t j=0;j<i;++j) if(f.starts[i]<f.starts[j]+root_bytes[j]&&f.starts[j]<f.starts[i]+root_bytes[i]) return bad("Overlapping frozen root arrays");
    }
    std::set<std::size_t> name_fields;
    for(std::size_t i=0;i<f.counts[0];++i) name_fields.insert(f.starts[0]+i*20);
    for(std::size_t i=0;i<f.counts[3];++i) name_fields.insert(f.starts[3]+i*12);
    std::size_t budget=f.content.size()*4;
    auto array=[&](std::size_t h,std::size_t width,std::size_t align,std::size_t& start,std::size_t& count) {
        auto raw=get(f.content,h,8); count=std::size_t(get(f.content,h+8));
        if(get(f.content,h+12)!=count) return false;
        if(!count) { start=0; return raw==0; }
        if(!(raw&1)||count>f.content.size()/width) return false;
        const auto target=std::int64_t(h)+(std::bit_cast<std::int64_t>(raw)>>1);
        const auto bytes=count*width;
        if(target<std::int64_t(payload_begin)||target%std::int64_t(align)||!range(f.content,std::uint64_t(target),bytes)||bytes>budget) return false;
        budget-=bytes; start=std::size_t(target); return true;
    };
    auto string_field=[&](std::size_t h) {
        std::size_t start,count;
        if(!array(h,2,2,start,count)) return false;
        if(!count) return true;
        if(get(f.content,start+(count-1)*2,2)) return false;
        for(std::size_t i=0;i+1<count;++i) {
            auto c=get(f.content,start+i*2,2);
            if(!c|| (c>=0xdc00&&c<=0xdfff)) return false;
            if(c>=0xd800&&c<=0xdbff) {
                if(i+2>=count) return false;
                auto low=get(f.content,start+(++i)*2,2); if(low<0xdc00||low>0xdfff) return false;
            }
        }
        return true;
    };
    for(std::size_t r=0;r<f.counts[4];++r) for(const auto& member:members) {
        const auto field=f.starts[4]+r*f.stride+member.offset;
        if(std::string_view(member.name).ends_with("_Array")) {
            const std::size_t width=member.tag==2?1:member.tag==10?16:member.tag==11?8:4;
            const std::size_t alignment=member.tag==10?8:member.tag==11?4:width;
            std::size_t start,count;
            if(!array(field,width,alignment,start,count)) return bad("Frozen typed array bounds/capacity");
            for(std::size_t i=0;i<count;++i) {
                if(member.tag==10&&!string_field(start+i*16)) return bad("Frozen string-array payload");
                if(member.tag==11) name_fields.insert(start+i*8);
            }
        } else if(member.tag==10) {
            if(!string_field(field)) return bad("Frozen UTF16 string payload");
        } else if(member.tag==11) name_fields.insert(field);
        else if(member.tag==9&&!std::isfinite(std::bit_cast<float>(std::uint32_t(get(f.content,field))))) return bad("Frozen nonfinite scalar");
    }
    for(auto field:name_fields) if(get(f.content,field,8)) return bad("Frozen name field is not a fixup placeholder");
    for(auto [field,name]:f.fixes) { (void)name; if(!name_fields.contains(field)) return bad("Frozen fixup patches a non-name field"); }
    return Status::ok_status();
}
std::string fname(const Frozen& f,const NativeZenPackage& p,std::size_t off) {
    auto it=f.fixes.find(off); if(it==f.fixes.end()) return "None";
    auto [i,n]=it->second; return p.names[i]+(n?"_"+std::to_string(n-1):"");
}
Status older_source_metadata(const Bytes& b,const NativeZenPackage& p,std::size_t role) {
    constexpr const char* packages[]{"/Game/Sound/BGM/bgm_piano_01","/Game/DataObject/Resident/BGMList",
        "/Game/DataObject/Resident/PianoScoreInfo","/Game/DataObject/Resident/PianoScore"};
    constexpr const char* objects[]{"bgm_piano_01","BGMList","PianoScoreInfo","PianoScore"};
    if(p.package_name!=packages[role]||p.export_name!=objects[role]||get(b,4)||get(b,p.export_at+20)||
       get(b,16)!=0x80802200) return bad("Older source canonical package/object/flags profile");
    std::set<std::uint64_t> expected{native_object_index(p.class_path,1),
        native_object_index(p.class_path.substr(0,p.class_path.find('.'))+".Default__"+p.class_path.substr(p.class_path.find('.')+1),1),
        native_object_index(p.class_path.substr(0,p.class_path.find('.')),1)};
    std::set<std::uint64_t> actual; for(std::size_t i=0;i<3;++i) actual.insert(get(b,p.import_at+i*8,8));
    if(actual!=expected) return bad("Older source canonical script import profile");
    return Status::ok_status();
}
Status older_rows(const std::array<Frozen,3>& f,const std::array<NativeZenPackage,4>& p) {
    const std::set<std::string> scores{"Journey","TifasTheme","BarretTheme","SyncoDeChocobo","DifficultToStandOnTwoLegs","AerithsTheme","Fighters","OneWingedAngel"};
    for(auto index:{1,2}) if(std::set<std::string>(f[index].keys.begin(),f[index].keys.end())!=scores) return bad("Older source canonical score row identities");
    std::set<std::string> cues;
    for(int i=1;i<=8;++i) {
        const auto cue="bgm_piano_0"+std::to_string(i); cues.insert(cue);
        if(std::find(f[0].keys.begin(),f[0].keys.end(),cue)==f[0].keys.end()) return bad("Older source missing canonical piano BGM row");
    }
    for(auto row:f[1].key_rows) if(!cues.contains(fname(f[1],p[2],f[1].starts[4]+row*f[1].stride+80))) return bad("Older score/BGM row pairing");
    return Status::ok_status();
}
Status older_music_names(const NativeZenPackage& p,const Bytes& body) {
    std::vector<std::string> expected{"/Game/Sound/BGM/bgm_piano_01","/Script/CoreUObject","/Script/SQEXSEAD","bgm_piano_01",
        "bgm_piano_01_Lv1_[Master]","BoolProperty","bStreamingAsset_PlatformPS5","bStreamingAsset_PlatformSW2","bStreamingAsset_PlatformXSX",
        "Class","Default__SQEXSEADMusic","Duration","FloatProperty","IntProperty","MapProperty","Mode0","Mode1","Mode2",
        "ModeNameIndexTable","NameProperty","None","Package","SectionNameIndexTable","SoundIndex","SoundNameIndexTable",
        "SQEXSEADMusic","StructProperty","VoicedSectionAdjustmentTime","Volume"};
    if(p.names.size()==28) expected.erase(expected.begin()+7);
    if(p.names!=expected||body.size()<144) return bad("Older Music canonical NameMap profile");
    for(std::size_t i=0;i<3;++i) {
        const auto at=0x26+i*12,index=get(body,at);
        if(index>=p.names.size()||get(body,at+4)||p.names[std::size_t(index)]!="Mode"+std::to_string(i)) return bad("Older Music mode key/NameMap mismatch");
    }
    return Status::ok_status();
}
Status single_row(const Bytes& source,const NativeZenPackage& p,const Frozen& original,const std::string& old_key,
    const std::string& new_key,const std::string& resource,const std::string& package,Bytes& output) {
    Frozen f=original; auto selected=std::find(f.keys.begin(),f.keys.end(),old_key); if(selected==f.keys.end()) return bad("Requested base score/BGM row absent");
    auto row=f.starts[4]+f.key_rows[std::size_t(selected-f.keys.begin())]*f.stride;
    auto names=p.names; names.push_back(package); names.push_back(new_key);
    auto key_index=std::uint32_t(names.size()-1),resource_index=key_index;
    if(!resource.empty()&&resource!=new_key) { names.push_back(resource); resource_index=std::uint32_t(names.size()-1); }
    const auto key=f.starts[0];
    for(auto [at,pair]:f.fixes) { (void)pair; if(at>key && at<key+20) return bad("Unexpected key name overlap"); }
    put(f.content,key,0,8); put(f.content,key+8,0); put(f.content,key+12,0xffffffff); put(f.content,key+16,0); f.fixes[key]={key_index,0};
    for(std::size_t h:{0,16,40}) { put(f.content,h+8,1); put(f.content,h+12,h==40?0:1); }
    put(f.content,f.starts[1],1); put(f.content,f.starts[2],0); put(f.content,72,((row-72)<<1)|1,8); put(f.content,80,1); put(f.content,84,1);
    if(!resource.empty()) {
        auto prop=f.properties.find("BGMName"); if(prop==f.properties.end()||!f.fixes.contains(row+prop->second)) return bad("Missing qualified BGMName fixup");
        f.fixes[row+prop->second]={resource_index,0};
    }
    // No payload relocation: every row array, string and chart note remains at its
    // original relative address. Only the reachable row and grouped FNames change.
    if(!std::equal(f.content.begin()+row,f.content.begin()+row+f.stride,original.content.begin()+row)) return bad("Carrier row bytes changed");
    Bytes body=f.prefix; append(body,f.content);
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<std::size_t>> groups;
    for(auto [at,pair]:f.fixes) groups[pair].push_back(at);
    add(body,0); add(body,0); add(body,groups.size());
    for(auto& [pair,offsets]:groups) { add(body,pair.first); add(body,pair.second); add(body,offsets.size()); for(auto at:offsets) add(body,at); }
    append(body,f.tail);
    auto status=rebuild_native_zen_package(source,p.role,package,p.export_name,names,body,output); if(!status.ok()) return status;
    NativeZenPackage check; status=inspect_native_zen_package(output,p.role,check); if(!status.ok()) return status;
    Frozen checked; status=frozen(output,check,checked); if(!status.ok()) return status;
    if(checked.keys!=std::vector<std::string>{new_key} || checked.key_rows!=std::vector<std::size_t>{0} || checked.starts[4]!=row || checked.content!=f.content || checked.fixes!=f.fixes) return bad("Single-row projection verification failed");
    if(!resource.empty() && fname(checked,check,row+checked.properties.at("BGMName"))!=resource) return bad("Single-row BGM identity verification failed");
    return Status::ok_status();
}
Bytes empty_pak() {
    // Pinned retoc/repak's empty v11 archive, mount ../../../, seed 0. Format
    // bytes/checksums only: empty primary/path/full-directory indexes, no assets.
    const std::string encoded="0a0000002e2e2f2e2e2f2e2e2f00000000000000000000000000010000007200000000000000080000000000000005fe405753166f125559e7c9ac558654f107c7e9010000007a0000000000000004000000000000009069ca78e7450a285173431b3e52c5c25299e473";
    Bytes b; auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<encoded.size();i+=2) b.push_back(std::uint8_t(digit(encoded[i])*16+digit(encoded[i+1])));
    b.resize(143); add(b,0x5a6f12e1); add(b,11); add(b,0,8); add(b,114,8);
    const std::string hash="ace857fc068a791ca324a99e384f0629d18c6c4d"; for(std::size_t i=0;i<hash.size();i+=2) b.push_back(std::uint8_t(digit(hash[i])*16+digit(hash[i+1])));
    b.resize(347); return b;
}
std::uint64_t file_hash(std::span<const std::uint8_t> b) { auto h=native_sha256(b); return get(h,0,8); }
NativeAudioArtifactSet projection(const NativeAudioCohortRequest& request,const NativeAudioCohortResult& b,const std::filesystem::path& root) {
    NativeAudioArtifactSet set; set.build_id=request.build_id; set.identity=request.identity; set.container_prefix=root/b.content_id/("native_"+b.content_id); set.container_id=b.container.container_id;
    set.base_score_name=request.base_score_name;
    set.packages=b.container.packages; set.music_name=b.music_name; set.bgm_key=b.bgm_key; set.score_name=b.score_name;
    const auto pak=empty_pak();
    const std::array<std::span<const std::uint8_t>,3> bytes{pak,b.container.utoc,b.container.ucas}; const char* ext[]{".pak",".utoc",".ucas"};
    for(std::size_t i=0;i<3;++i) { auto path=set.container_prefix; path+=ext[i]; set.files[i]={path,bytes[i].size(),file_hash(bytes[i])}; }
    return set;
}
Status verify_files(const NativeAudioCohortResult& b,const NativeAudioArtifactSet& set,const NativeAudioArtifactSet& expected_set) {
    const auto pak=empty_pak();
    const std::array<std::span<const std::uint8_t>,3> expected{pak,b.container.utoc,b.container.ucas};
    std::array<std::uint8_t,65536> buffer;
    for(std::size_t i=0;i<3;++i) {
        std::error_code ec; const auto& f=set.files[i];
        if(std::filesystem::is_symlink(f.path,ec)||ec||std::filesystem::file_size(f.path,ec)!=expected[i].size()||ec||f.bytes!=expected_set.files[i].bytes||f.content_hash!=expected_set.files[i].content_hash) return bad("Native cache file extent/hash metadata");
        std::ifstream in(f.path,std::ios::binary);
        for(std::size_t at=0;at<expected[i].size();) {
            const auto n=std::min(buffer.size(),expected[i].size()-at);
            if(!in.read(reinterpret_cast<char*>(buffer.data()),std::streamsize(n))||
                !std::equal(buffer.begin(),buffer.begin()+n,expected[i].begin()+at))
                return bad("Native cache file differs; immutable outputs will not be replaced");
            at+=n;
        }
        if(!in||in.peek()!=std::char_traits<char>::eof()) return bad("Native cache file differs; immutable outputs will not be replaced");
    }
    return Status::ok_status();
}
}
NativeAudioCohortResult build_native_audio_cohort(const NativeAudioCohortRequest& request) {
    NativeAudioCohortResult result;
    const auto& source_zen=request.source_snapshot?*request.source_snapshot:request.source_zen;
    const bool older=request.build_id==kOlderBuild;
    if((request.build_id!=kCurrentBuild&&!older)||!valid_native_audio_identity(request.identity)||!ascii(request.base_score_name)||request.base_score_name.size()>128) { result.status=bad("Unsupported native cohort build/identity/base score"); return result; }
    // Stock template receipts: preserved Music and the independently decoded
    // BGMList/PianoScoreInfo/PianoScore controls, not diagnostic patched assets.
    constexpr const char* hashes[]{"707fcad468b95724d09031fdfe83c2103c1e7a7aa1330822e8bc973992f53559","6d7ce8582def003fe46e76b653b0e9f0294e25eff7ac9bb8dcb77d98976d580e","b7184b8520c1eae265dd30ba5c6f4f072ec0b160516d7770caca9f22f96f5dad","a2dc74150a2d50f8f2e33e35c898fe64a370dc06a3ee0714c817ffa0aebfeffa"};
    std::array<NativeZenPackage,4> p; std::array<Frozen,3> f;
    Bytes identity; string(identity,older?"FF7RP-native-cohort-old1004-forced-nonstreaming-suffix40-v2-20261017":"FF7RP-native-cohort-v3-canonical-bgm-cue-20261016"); string(identity,request.build_id); string(identity,request.identity); string(identity,request.base_score_name);
    add(identity,request.timing.logical_source_frames,8); add(identity,request.timing.sample_rate,4);
    if(request.mabf.size()>64*1024*1024) { result.status=bad("Native cohort MABF size"); return result; }
    for(std::size_t i=0;i<4;++i) {
        if(source_zen[i].size()>64*1024*1024) { result.status=bad("Native template SHA256 provenance mismatch"); return result; }
        const auto digest=native_sha256(source_zen[i]);
        if(!older&&hex(digest)!=hashes[i]) { result.status=bad("Native template SHA256 provenance mismatch"); return result; }
        result.status=inspect_native_zen_package(source_zen[i],NativeAudioPackageRole(i),p[i]); if(!result.status.ok()) return result;
        if(older) { result.status=older_source_metadata(source_zen[i],p[i],i); if(!result.status.ok()) return result; }
        append(identity,digest);
        if(i) { result.status=frozen(source_zen[i],p[i],f[i-1],true); if(!result.status.ok()) return result; }
    }
    if(older) { result.status=older_rows(f,p); if(!result.status.ok()) return result; }
    auto digest=native_sha256(request.mabf); append(identity,digest); result.content_id=hex(native_sha256(identity));
    result.music_name="bgm_native_"+result.content_id; result.bgm_key="bgm_"+result.content_id; result.score_name="score_"+result.content_id;
    const auto ns="/Game/FF7RPianoSongs/"+result.content_id+"/";
    auto selected=std::find(f[1].keys.begin(),f[1].keys.end(),request.base_score_name);
    if(selected==f[1].keys.end()||!f[1].properties.contains("BGMName")) { result.status=bad("Requested base score absent from supported stock template"); return result; }
    auto row=f[1].starts[4]+f[1].key_rows[std::size_t(selected-f[1].keys.begin())]*f[1].stride;
    auto bgm=fname(f[1],p[2],row+f[1].properties.at("BGMName"));
    Bytes music_body(source_zen[0].begin()+p[0].body_at,source_zen[0].end());
    if(older&&music_body.size()>=10&&music_body[5]==6&&hex(native_sha256(source_zen[0]))!=hashes[0]) { result.status=bad("Only SHA-pinned current Music may be transformed to the older schema"); return result; }
    if(older) { result.status=older_music_names(p[0],music_body); if(!result.status.ok()) return result; }
    auto music=build_native_music_export(music_body,request.mabf,request.timing,
        older?NativeMusicExportProfile::Older1004:NativeMusicExportProfile::Current1005); if(!music.status.ok()) { result.status=music.status; return result; }
    if((p[0].names.size()!=29&&!(older&&p[0].names.size()==28)) || p[0].names[0]!="/Game/Sound/BGM/bgm_piano_01" ||
       p[0].names[3]!="bgm_piano_01" || p[0].names[4]!="bgm_piano_01_Lv1_[Master]" ||
       p[0].package_name!=p[0].names[0] || p[0].export_name!=p[0].names[3] ||
       get(source_zen[0],0,8)!=0 || get(source_zen[0],p[0].export_at+16,8)!=3 ||
       get(music_body,0x52,8)!=3) { result.status=bad("Unsupported Music cue/package NameMap profile"); return result; }
    // Native queue lookup uses this exact package path. Retarget the qualified
    // slots so the preserved (3,0) inner cue also names the generated resource.
    auto names=p[0].names; auto music_package="/Game/Sound/BGM/"+result.music_name;
    names[0]=music_package; names[3]=result.music_name;
    for(std::size_t i=0;i<4;++i) result.packages[i].role=NativeAudioPackageRole(i);
    result.status=rebuild_native_zen_package(source_zen[0],NativeAudioPackageRole::Music,music_package,result.music_name,names,music.bytes,result.packages[0].zen); if(!result.status.ok()) return result;
    if(older) put(result.packages[0].zen,16,get(source_zen[0],16)|0x80802200);
    const std::string objects[]{"","BGMList","PianoScoreInfo","PianoScore"};
    for(std::size_t i=1;i<4;++i) {
        result.status=single_row(source_zen[i],p[i],f[i-1],i==1?bgm:request.base_score_name,i==1?result.bgm_key:result.score_name,i==1?result.music_name:i==2?result.bgm_key:"",ns+objects[i],result.packages[i].zen);
        if(!result.status.ok()) return result;
    }
    result.container=build_native_iostore("native_"+result.content_id,result.packages); result.status=result.container.status; return result;
}
Status verify_native_audio_cohort(const NativeAudioCohortRequest& request,const NativeAudioArtifactSet& set) {
    auto b=build_native_audio_cohort(request); if(!b.status.ok()) return b.status;
    auto expected=projection(request,b,set.container_prefix.parent_path().parent_path());
    if(set.build_id!=expected.build_id||set.identity!=expected.identity||set.base_score_name!=expected.base_score_name||set.container_prefix!=expected.container_prefix||set.container_id!=expected.container_id||set.music_name!=expected.music_name||set.bgm_key!=expected.bgm_key||set.score_name!=expected.score_name) return bad("Native cohort descriptor identity mismatch");
    for(std::size_t i=0;i<4;++i) { auto& a=set.packages[i]; auto& e=expected.packages[i]; if(a.role!=e.role||a.object_path!=e.object_path||a.class_path!=e.class_path||a.package_id!=e.package_id||a.public_export_hash!=e.public_export_hash) return bad("Native cohort package descriptor mismatch"); }
    for(std::size_t i=0;i<3;++i) if(set.files[i].path!=expected.files[i].path) return bad("Native cohort file path mismatch");
    return verify_files(b,set,expected);
}
Status prepare_native_audio_cohort(const NativeAudioCohortRequest& request,const std::filesystem::path& root,NativeAudioArtifactSet& output) {
    output={}; if(!root.is_absolute()) return bad("Native cache root must be absolute");
    auto b=build_native_audio_cohort(request); if(!b.status.ok()) return b.status;
    auto set=projection(request,b,root); std::error_code ec; std::filesystem::create_directories(root,ec);
    if(ec) return Status::error(StatusCode::IoError,"Cannot create native cache root");
    auto dir=set.container_prefix.parent_path(); const auto created=std::filesystem::create_directory(dir,ec);
    if(ec||std::filesystem::is_symlink(dir,ec)||ec) return Status::error(StatusCode::IoError,"Cannot create safe native cache content directory");
    if(created) {
        const auto pak=empty_pak();
        const std::array<std::span<const std::uint8_t>,3> bytes{pak,b.container.utoc,b.container.ucas};
        for(std::size_t i=0;i<3;++i) { std::ofstream out(set.files[i].path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes[i].data()),std::streamsize(bytes[i].size())); out.close(); if(!out) return Status::error(StatusCode::IoError,"Native cache write failed; partial immutable directory retained"); }
    }
    auto s=verify_files(b,set,set); if(s.ok()) output=std::move(set); return s;
}
} // namespace ff7rp::pipeline
