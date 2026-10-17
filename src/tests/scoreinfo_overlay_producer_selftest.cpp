// Exercise the actual StyleSetup row producer, not a replacement/helper model.
#include "game/scoreinfo_overlay.cpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace ff7r::piano::game;
// Unrelated UI/hook integration must never execute in this producer test.
namespace ff7r::piano::game {
core::HookCallbackGate& non_audio_hook_gate() {std::abort();}
void push_title_resolver_token(const TitleResolverToken&) {std::abort();}
void invalidate_title_resolver_tokens(const CustomContextToken&) {std::abort();}
}
namespace {
unsigned constructor_calls=0;
void* __fastcall forbidden_stock_constructor(FNameValue* out,const wchar_t*,int32_t) {
    ++constructor_calls;*out={0x00990099,0};return out;
}
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main() {
    try {
        // The previous producer would call this constructor and replace the cue.
        g_module_base=reinterpret_cast<std::uintptr_t>(&forbidden_stock_constructor)-rva::FNameCtor;
        SongDescriptor song;song.title=L"song";
        SongDifficultyProfile profile;profile.title=L"custom profile";profile.difficulty=5;
        profile.mode_change_combo_counts={11,29};profile.score_thresholds={101,203,307,409};
        std::array<std::uint8_t,ScoreInfoOverlayRow::kRowSize> source{};
        const FNameValue custom_cue{0x1234abcd,0x00010203};
        write_row_field(source,kScoreInfoBgmNameOffset,custom_cue);
        write_row_field(source,kScoreInfoCameraMinOffset,1.25f);
        write_row_field(source,kScoreInfoCameraMaxOffset,8.5f);
        write_row_field(source,kScoreInfoCameraRateOffset,0.75f);
        write_row_field(source,kScoreInfoBpmOffset,137.0f);
        write_row_field(source,kScoreInfoStreamingFrameOffset,std::uint32_t{23});
        source[0x70]=0x9d;
        require(scoreinfo_result_catalog_role_from_rva(rva::ScoreInfoStyleSetupReturn)
            ==ScoreInfoResultCatalogRole::StyleSetup,"StyleSetup role");
        auto row=make_scoreinfo_overlay_row(song,&profile,source.data(),source.size());
        require(scoreinfo_overlay_publishable(true,row),"custom row must publish");
        const auto exact_cue=[&](const ScoreInfoOverlayRow& value) {
            return std::memcmp(value.data()+kScoreInfoBgmNameOffset,
                source.data()+kScoreInfoBgmNameOffset,sizeof(custom_cue))==0;
        };
        require(exact_cue(row),"producer changed genuine eight-byte custom cue");
        require(row.title==profile.title && row.mode_change_counts==profile.mode_change_combo_counts
            && row.score_thresholds==profile.score_thresholds && row.difficulty==5,"profile projections");
        require(row.camera_min==1.25f && row.camera_max==8.5f && row.camera_rate==0.75f
            && row.bpm==137.0f && row.streaming_frame==23 && row.row[0x70]==0x9d,"source projections");
        const auto changes=read_row_field<TArrayView<int32_t>>(row.row,kScoreInfoModeChangeOffset);
        const auto scores=read_row_field<TArrayView<int32_t>>(row.row,kScoreInfoScoreArrayOffset);
        require(changes.data==row.mode_change_counts.data() && scores.data==row.score_thresholds.data(),"owned arrays");
        auto copied=row;auto moved=std::move(copied);moved.refresh_references();
        require(exact_cue(moved),"copy/move/refresh changed cue");
        const auto rejected=[&](const void* data,std::size_t size) {
            return !scoreinfo_overlay_publishable(true,make_scoreinfo_overlay_row(song,&profile,data,size));
        };
        require(rejected(nullptr,0),"missing source accepted");
        require(rejected(source.data(),kScoreInfoBgmNameOffset+sizeof(custom_cue)-1),"truncated cue accepted");
        require(rejected(reinterpret_cast<void*>(1),source.size()),"unreadable source accepted");
        write_row_field(source,kScoreInfoBgmNameOffset,FNameValue{0,7});
        require(rejected(source.data(),source.size()),"None comparison ID accepted");
        require(constructor_calls==0,"producer invoked native name constructor");
        std::cout<<"scoreinfo overlay producer selftest passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
