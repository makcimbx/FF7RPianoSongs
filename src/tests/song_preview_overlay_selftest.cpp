#include "game/song_preview_overlay.h"
#include "pipeline/song_preview.h"

#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace ff7r::piano::game;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try {
        PreviewSelectedRowBounds bounds{{1674.667f,642.667f,661.333f,77.333f},
            {1634.667f,157.333f,728,561.334f}};
        const auto layout = preview_badge_layout(bounds,2560,1440);
        require(layout.has_value(),"observed row admits external Canvas badge");
        require(layout->panel.width == 200 && layout->panel.x+200 == bounds.row.x-32,
            "badge follows row with room for native selection diamond");
        auto first = bounds; first.row.y = first.inherited_cull.y;
        require(preview_badge_layout(first,2560,1440).has_value(),"first scroll row");
        auto bad = bounds; bad.inherited_cull.x = bounds.row.x-27;
        require(preview_badge_layout(bad,2560,1440).has_value(),"external badge is not a clipped native child");
        bad = bounds; bad.row.x = 100;
        require(!preview_badge_layout(bad,2560,1440),"insufficient viewport space hides");
        bad = bounds; bad.inherited_cull.height = 20;
        require(!preview_badge_layout(bad,2560,1440),"vertical clipping hides, never relocates");
        bad = bounds; bad.row.x = std::numeric_limits<float>::quiet_NaN();
        require(!preview_badge_layout(bad,2560,1440),"nonfinite row");
        require(!preview_badge_layout(bounds,std::numeric_limits<float>::infinity(),1440),"nonfinite viewport");
        bad = bounds; bad.row.y = -1;
        require(!preview_badge_layout(bad,2560,1440),"offscreen row");

        auto artifact = std::make_shared<ff7rp::pipeline::SongPreviewArtifact>();
        artifact->frame_count = 480000;
        for (std::size_t i = 0; i < 64; ++i) artifact->waveform[i] = (i % 4 == 3) ? .1f : .01f;
        SongDescriptor song; song.preview = artifact;
        SongDifficultyProfile profile;
        SongPreviewRenderSnapshot snapshot;
        snapshot.session_generation = 1;
        snapshot.selection.storage = artifact;
        snapshot.selection.song = &song; snapshot.selection.profile = &profile;
        snapshot.mode = ff7r::piano::core::PreviewMode::Auto;
        snapshot.phase = PreviewPhase::Playing;
        snapshot.position = {true,false,240000,480000};
        for (std::size_t b = 0; b < snapshot.spectrum.size(); ++b) snapshot.spectrum[b] = b / 15.0f;
        const auto& wave = layout->waveform;
        const auto& line = layout->progress;
        std::vector<float> bars;
        float playhead = -1, alpha = 0;
        std::wstring label;
        const OverlayDrawText text = [&](void*,float x,float y,const wchar_t* value,void*,
            const ff7r::piano::game::runtime_layouts::FLinearColor*) {
            require(x > layout->panel.x && y > layout->panel.y && y+16 < wave.y,"text row above bars");
            label = value; return 16;
        };
        const OverlayDrawTile tile = [&](void*,float x,float y,float w,float h,float,float,float,float,
            const ff7r::piano::game::runtime_layouts::FLinearColor* color,const void*,bool) {
            require(std::isfinite(x) && std::isfinite(y) && w>0 && h>0,"finite positive draw");
            require(x >= layout->panel.x && y >= layout->panel.y
                && x+w <= layout->panel.x+layout->panel.width+.001f
                && y+h <= layout->panel.y+layout->panel.height+.001f,"all tiles contained");
            if (y >= wave.y && y+h <= wave.y+wave.height+.001f && h > 2
                && color->b > .9f && color->r < .5f) bars.push_back(h);
            alpha = std::max(alpha, color->a);
            if (y == line.y-2 && color->r == 1) playhead = x;
            return true;
        };
        require(draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,text,tile),"draw");
        require(bars.size() == 15 && bars.back() == wave.height,
            "equalizer bars follow live band levels; silent band stays a 2px floor");
        require(label == L"0:05 / 0:10","device clock while playing");
        require(std::abs(playhead - (line.x + (line.width-2)*.5f)) < .001f,
            "device playhead, excerpt spans an unknown-length song");
        artifact->source_start_seconds = 60; song.duration_seconds = 200;
        require(draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,text,tile),"song draw");
        require(label == L"1:05 / 3:20","song clock is excerpt start plus device position over the song");
        require(std::abs(playhead - (line.x + line.width*.3f + (line.width*.05f-2)*.5f)) < .01f,
            "playhead inside the excerpt window of the song timeline");
        bars.clear(); playhead = -1; label.clear();
        snapshot.phase = PreviewPhase::Loading; snapshot.position = {}; snapshot.spectrum = {};
        require(draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,text,tile),"loading draw");
        require(bars.empty() && playhead < 0 && label == L"1:00 / 3:20","loading shows the excerpt start and a floor");
        alpha = 0; label.clear();
        require(draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,text,tile,.4f),"fading draw");
        require(std::abs(alpha - .4f) < .001f && label.empty(),"fade scales every tile; opaque-shadow text waits");
        require(!draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,text,tile,0),
            "transparent badge is not drawn");
        menu_theme_plan::PreviewPalette custom;
        const menu_theme_plan::PreviewPalette defaults;
        require(defaults.linear == std::array<std::array<float,4>,5>{{
            {.012f,.024f,.30f,.94f}, {.64f,.49f,.22f,1}, {.12f,.66f,.94f,1}, {.03f,.07f,.45f,1}, {1,1,1,1}}},
            "Original preserves all five exact pre-theme linear native-look roles");
        for (unsigned role = 0; role < custom.linear.size(); ++role)
            custom.linear[role] = {.1f * (role + 1), .02f * (role + 1), .03f, .1f * (role + 1)};
        custom.overrides = 31;
        std::vector<runtime_layouts::FLinearColor> drawn;
        runtime_layouts::FLinearColor drawn_text{};
        const OverlayDrawTile collect = [&](void*, float, float, float, float, float, float, float, float,
            const runtime_layouts::FLinearColor* color, const void*, bool) { drawn.push_back(*color); return true; };
        const OverlayDrawText collect_text = [&](void*, float, float, const wchar_t*, void*, const runtime_layouts::FLinearColor* color) {
            drawn_text = *color; return 16;
        };
        require(draw_song_preview_badge(snapshot, *layout, reinterpret_cast<void*>(1), nullptr, collect_text, collect, .75f, custom),
            "custom value palette draws same badge geometry");
        const auto matches = [&](const runtime_layouts::FLinearColor& color, unsigned role) {
            const auto& c = custom.linear[role]; return color.r == c[0] && color.g == c[1] && color.b == c[2]
                && std::abs(color.a - c[3] * .75f) < .000001f;
        };
        require(matches(drawn[0], 1) && matches(drawn[1], 0) && matches(drawn[2], 4)
            && matches(drawn[3], 2) && matches(drawn[4], 4) && matches(drawn[drawn.size()-2], 3)
            && matches(drawn.back(), 1) && matches(drawn_text, 4),
            "all roles: border/excerpt, fill, spectrum, groove, text/icon/peaks; fade multiplies authored alpha");
        snapshot.phase = PreviewPhase::Playing; snapshot.position = {true,false,240000,480000}; drawn.clear();
        require(draw_song_preview_badge(snapshot, *layout, reinterpret_cast<void*>(1), nullptr, collect_text, collect, .75f, custom)
            && matches(drawn.back(), 4), "custom Text role also colors actual playing playhead, preserving fade alpha");
        auto transparent_text = custom; transparent_text.linear[4][3] = 0;
        drawn_text.a = -1;
        require(draw_song_preview_badge(snapshot, *layout, reinterpret_cast<void*>(1), nullptr, collect_text, collect, 1, transparent_text)
            && drawn_text.a == -1, "fully transparent authored text cannot leave opaque native shadow");
        publish_song_preview_palette(1, custom);
        menu_theme_plan::PreviewPalette copied;
        require(preview_badge_palette(capture_song_preview_palette(), 1, copied) && copied == custom,
            "same-session fade uses copied committed palette");
        publish_song_preview_palette(1, {});
        require(preview_badge_palette(capture_song_preview_palette(), 1, copied) && copied == menu_theme_plan::PreviewPalette{},
            "Original replaces fade colors with exact defaults without stale custom fallback");
        retire_song_preview_palette(1);
        require(!preview_badge_palette(capture_song_preview_palette(), 1, copied), "closed-session fade suppressed");
        publish_song_preview_palette(2, custom);
        require(!preview_badge_palette(capture_song_preview_palette(), 1, copied)
            && preview_badge_palette(capture_song_preview_palette(), 2, copied), "next owner never leaks style onto old shown selection");
        require(!preview_badge_palette(capture_song_preview_palette(), 3, copied),
            "active publication cannot authorize mismatched newer Preview owner");
        retire_song_preview_palette(2);
        require(!preview_badge_palette(capture_song_preview_palette(), 2, copied)
            && !preview_badge_palette(capture_song_preview_palette(), 1, copied),
            "retired generation and older fading selections remain suppressed");
        require(preview_badge_palette(capture_song_preview_palette(), 3, copied)
            && copied == menu_theme_plan::PreviewPalette{},
            "terminal retirement then genuinely newer Preview without theme publication retains original working badge");
        const auto retired = capture_song_preview_palette();
        require(!retired.active && retired.generation == 2 && retired.palette == custom,
            "default fallback does not revive publication or reuse retired custom colors");
        snapshot.mode = ff7r::piano::core::PreviewMode::Off;
        require(!draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,{},tile),"off hidden");
        snapshot.mode = ff7r::piano::core::PreviewMode::Auto;
        snapshot.position.failed=true;
        require(!draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,{},tile),"driver failed hidden");
        snapshot.position.failed=false;
        require(!draw_song_preview_badge(snapshot,*layout,reinterpret_cast<void*>(1),nullptr,{},
            [](auto...){return false;}),"tile failure failclosed");
        std::puts("song preview overlay selftest passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
