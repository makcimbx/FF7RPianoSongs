#include "game/song_hud_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace ff7r::piano::game;
static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        // Reference composition: square left of title, metadata under its separator.
        const SongHudAnchor anchor{{1280,90,350,28},1674,942,1,SongHudRect{1248,86,390,42}};
        const SongHudFontMetrics metrics{16, [](std::wstring_view s) {
            return static_cast<float>(s.size()) * 8;
        }};
        const auto layout = song_hud_layout(anchor, metrics.line_height, true);
        require(layout && layout->cover && layout->metadata, "reference layout");
        const auto c = *layout->cover, r = *layout->metadata;
        require(c.x == 1130 && c.y == 49 && c.width == 110 && c.height == 110,
            "reference artwork proportions independent of title-widget padding");
        require(c.x+c.width+8 == r.x && r.x == anchor.title_block->x && r.x<anchor.title.x && r.y == 126,
            "metadata expands left beside the cover without overlapping it or moving the native title");
        require(r.height == 16, "fixed native font height");
        require(r.x+r.width == anchor.viewport_width-8 && r.width>anchor.title.width,
            "metadata uses available Canvas width with a right gutter, not native title width");
        auto scaled = anchor; scaled.ui_scale = 2;
        const auto scaled_layout = song_hud_layout(scaled,16,true);
        require(scaled_layout && scaled_layout->metadata->height == 16
            && scaled_layout->metadata->y == 134
            && scaled_layout->metadata->x+scaled_layout->metadata->width==anchor.viewport_width-16,
            "scale gaps and right gutter without double-scaling supplied font metrics");
        auto doubled = anchor;
        doubled.viewport_width *= 2; doubled.viewport_height *= 2; doubled.ui_scale *= 2;
        for (auto* rect : {&doubled.title,&*doubled.title_block}) {
            rect->x *= 2; rect->y *= 2; rect->width *= 2; rect->height *= 2;
        }
        require(song_hud_layout(doubled,32,true)->cover->width == 220,
            "artwork scales with Canvas resolution");
        auto padded = anchor; padded.title.height = 56; padded.title_block->height = 80;
        require(song_hud_layout(padded,16,true)->cover->width == c.width,
            "native title-widget padding does not enlarge artwork");
        const SongHudAnchor screenshot{{1562,105,390,38},2000,1125,1.04f,
            SongHudRect{1528,100,430,60}};
        const auto corrected = song_hud_layout(screenshot,19,true);
        require(corrected && corrected->cover
            && std::abs(corrected->cover->width-131.3694f) < .01f
            && corrected->cover->x+corrected->cover->width < screenshot.title_block->x,
            "owner screenshot proportions clear the note rather than making a 152px text-derived cover");
        auto missing_block = anchor; missing_block.title_block.reset();
        require(!song_hud_layout(missing_block,16,true)->cover
            && song_hud_layout(missing_block,16,true)->metadata->x==anchor.title.x,
            "missing complete-block evidence omits artwork and retains the qualified text anchor");
        auto incorrect_block = anchor; incorrect_block.title_block->x = anchor.title.x+1;
        require(!song_hud_layout(incorrect_block,16,true)->cover,
            "inconsistent complete-block bounds omit artwork");
        require(!song_hud_layout(anchor,0,true), "missing native font metrics");
        auto bad = anchor; bad.title.x = -1;
        require(!song_hud_layout(bad,16,true), "negative anchor");
        bad = anchor; bad.title.width = std::numeric_limits<float>::infinity();
        require(!song_hud_layout(bad,16,true), "nonfinite bounds");
        bad = anchor; bad.ui_scale = std::numeric_limits<float>::quiet_NaN();
        require(!song_hud_layout(bad,16,true), "nonfinite scale");
        bad = anchor; bad.viewport_width = 0;
        require(!song_hud_layout(bad,16,true), "invalid viewport");
        const SongHudAnchor tiny{{0,0,1,1},1,1,1};
        const auto tiny_layout = song_hud_layout(tiny,16,true);
        require(tiny_layout && !tiny_layout->cover && !tiny_layout->metadata,
            "tiny viewport suppresses rather than emitting clipped or negative rectangles");
        bad = anchor; bad.title.x = 20;
        const auto no_cover_space = song_hud_layout(bad,16,true);
        require(no_cover_space && !no_cover_space->cover && no_cover_space->metadata,
            "insufficient cover space preserves metadata");
        require(song_hud_metadata({},100,metrics).empty(), "no placeholders");
        const SongHudMetadata metadata{L"OST",L"Author",L"132 BPM",L"3:42"};
        require(song_hud_metadata(metadata,1000,metrics)
            == L"OST   132 BPM   3:42   Author", "only preparer credit appears, not original artist/composer");
        require(song_hud_metadata({L" \n",{},L"132 BPM",L"3:42"},1000,metrics)
            == L"132 BPM   3:42", "missing fields have no separators or placeholders");
        const auto truncated = song_hud_metadata(metadata,80,metrics);
        require(truncated.ends_with(L"...") && metrics.measure_width(truncated) <= 80,
            "measured ellipsis");
        require(song_hud_metadata(metadata,23,metrics).empty(), "ellipsis itself must fit");
        std::wstring unicode;
        for (int i = 0; i < 500; ++i) unicode += L"\u65e5\xd83c\xdfb5";
        const auto fitted = song_hud_metadata({unicode},96,metrics);
        require(fitted.ends_with(L"...") && metrics.measure_width(fitted) <= 96,
            "long Unicode row stays bounded");
        int measurements = 0;
        const SongHudFontMetrics counted{16, [&](std::wstring_view s) {
            ++measurements; return metrics.measure_width(s);
        }};
        (void)song_hud_metadata({unicode,unicode,unicode,unicode},96,counted);
        require(measurements <= 16, "bounded font measurements for long credits");
        for (std::size_t i = 0; i < fitted.size(); ++i) {
            const auto unit = static_cast<unsigned>(fitted[i]);
            if (unit >= 0xd800 && unit <= 0xdbff) {
                require(i+1 < fitted.size() && fitted[i+1] >= 0xdc00 && fitted[i+1] <= 0xdfff,
                    "no split surrogate pair");
                ++i;
            } else require(unit < 0xdc00 || unit > 0xdfff, "no orphan low surrogate");
        }
        require(song_hud_metadata({L"a\nb\u2028c"},1000,metrics) == L"a b c", "one text row");
        SongHudFontMetrics invalid_metrics{16, [](auto) { return std::numeric_limits<float>::quiet_NaN(); }};
        require(song_hud_metadata(metadata,100,invalid_metrics).empty(), "invalid measurements hide text");

        int canvas_token = 0, font_token = 0;
        const char opaque_resource = 0; // Deliberately not a UObject or a dereferenceable resource structure.
        std::vector<const void*> resources;
        std::wstring drawn;
        std::vector<std::wstring> labels;
        std::vector<float> text_positions;
        float drawn_scale=0, expected_alpha=1;
        const OverlayDrawTile tile = [&](void* canvas,float x,float y,float w,float h,
            float,float,float,float,const runtime_layouts::FLinearColor* color,const void* resource,bool blend) {
            require(canvas == &canvas_token && blend && color->a == 1, "Canvas contract");
            require(std::isfinite(x) && std::isfinite(y) && w>0 && h>0 && x>=0 && y>=0
                && w <= anchor.viewport_width-x && h <= anchor.viewport_height-y, "contained draws");
            resources.push_back(resource); return true;
        };
        const SongHudDrawText text = [&](void*,float x,float y,const wchar_t* value,void* font,
            const runtime_layouts::FLinearColor* color,float scale) {
            require(x >= r.x && x < r.x+r.width && y == r.y && font == &font_token && color->r == .8f
                && color->a == expected_alpha, "metadata follows native font, color and opacity");
            text_positions.push_back(x);
            drawn = value; drawn_scale=scale; labels.emplace_back(value);
            return 0; // Layout metric, not a success result.
        };
        require(draw_song_hud_overlay(anchor,metadata,metrics,&opaque_resource,&canvas_token,&font_token,text,tile),
            "draw succeeds independent of native text layout metric");
        require(resources.size() == 1 && resources.front() == &opaque_resource,
            "rounded prepared cover is one opaque resource draw, no square frame over alpha corners");
        require(labels==std::vector<std::wstring>({L"OST",L"132 BPM",L"3:42",L"Author"}),
            "drawing and native metric probes both omit the artist field");
        require(!drawn.empty() && metrics.measure_width(drawn)*drawn_scale <= r.width
            && drawn_scale == .875f, "measurement and drawing use identical uniform scale");
        const SongHudFontMetrics small_font{8,[](std::wstring_view s) { return s.size()*4.0f; }};
        require(draw_song_hud_overlay(anchor,metadata,small_font,nullptr,&canvas_token,&font_token,text,tile)
            && drawn_scale == 1.75f && small_font.measure_width(drawn)*drawn_scale <= r.width,
            "small native font enlarged to readable header-relative metadata");
        const SongHudFontMetrics pixels{13,[](std::wstring_view s) { return s.size()*7.25f; },1};
        text_positions.clear(); labels.clear();
        require(draw_song_hud_overlay(anchor,{{},L"By",L"90 BPM",L"1:48"},pixels,nullptr,
            &canvas_token,&font_token,text,tile) && drawn_scale==1 && text_positions.size()==3,
            "actual size-keyed pixel metrics draw unscaled, not fractional target-height magnification");
        require(std::abs(text_positions[1]-text_positions[0]-pixels.measure_width(L"90 BPM")-13*.55f)<.001f,
            "run advances use actual native pixel widths and heights without a legacy metric factor");
        const char tempo=1,clock=2,pencil=3;
        const SongHudIconTextures icons{&tempo,&clock,&pencil};
        auto short_title=anchor; short_title.title.width=100;
        labels.clear(); resources.clear();
        require(draw_song_hud_overlay(short_title,{L"STEINS;GATE 0",L"makcimbx",L"90 BPM",L"1:48"},
            metrics,nullptr,&canvas_token,&font_token,text,tile,1,icons)
            && labels==std::vector<std::wstring>({L"STEINS;GATE 0",L"90 BPM",L"1:48",L"makcimbx"}),
            "short title cannot prematurely truncate album when full icon-led row fits the Canvas");
        resources.clear(); text_positions.clear();
        require(draw_song_hud_overlay(anchor,{{},L"By",L"90 BPM",L"1:48"},metrics,nullptr,
            &canvas_token,&font_token,text,tile,1,icons), "icon-led metadata");
        require(resources==std::vector<const void*>({&tempo,&clock,&pencil})
            && text_positions.size()==3 && text_positions[0]>r.x
            && text_positions[0]<text_positions[1] && text_positions[1]<text_positions[2],
            "metronome, clock and pencil resources precede their own labels in order");
        require(std::abs(text_positions[0]-r.x-14*1.2f)<.001f
            && std::abs(text_positions[1]-text_positions[0]-metrics.measure_width(L"90 BPM")*.875f
                -14*(.55f+1.2f))<.001f,
            "compact icon-label and inter-group gaps share the same measured layout budget");
        resources.clear();
        require(draw_song_hud_overlay(anchor,{{},{},L"90 BPM",{}},metrics,nullptr,
            &canvas_token,&font_token,text,tile,1,icons) && resources==std::vector<const void*>({&tempo}),
            "missing credit/duration never draws orphan icons");
        labels.clear(); resources.clear();
        const std::wstring long_album(200,L'A');
        require(draw_song_hud_overlay(anchor,{long_album,{},L"90 BPM",L"1:48"},metrics,nullptr,
            &canvas_token,&font_token,text,tile,1,icons)
            && labels.size()==3 && labels.front().ends_with(L"...")
            && labels[1]==L"90 BPM" && labels[2]==L"1:48",
            "long soundtrack names truncate before consuming the metronome and duration");
        labels.clear(); resources.clear();
        require(draw_song_hud_overlay(anchor,{long_album,L"makcimbx",L"90 BPM",L"1:48"},metrics,nullptr,
            &canvas_token,&font_token,text,tile,1,icons)
            && labels.size()==4 && labels[1]==L"90 BPM" && labels[2]==L"1:48"
            && labels[3]==L"makcimbx"
            && std::find(resources.begin(),resources.end(),icons[2])!=resources.end(),
            "long album preserves the preparer credit and pencil without a second creator");
        resources.clear(); drawn.clear();
        require(draw_song_hud_overlay(anchor,{},metrics,nullptr,&canvas_token,nullptr,{},{}),
            "no artwork or fields is a no-op without placeholders");
        require(resources.empty() && drawn.empty(), "no blank frame or duplicate title");
        require(draw_song_hud_overlay(anchor,metadata,metrics,nullptr,&canvas_token,&font_token,text,tile)
            && resources.empty() && !drawn.empty(), "missing artwork retains metadata");
        require(!draw_song_hud_overlay(anchor,metadata,metrics,&opaque_resource,&canvas_token,&font_token,text,
            [](auto...) { return false; }), "tile failure propagates");
        const SongHudDrawText throws = [](auto...) -> int32_t { throw std::runtime_error("draw failed"); };
        require(!draw_song_hud_overlay(anchor,metadata,metrics,nullptr,&canvas_token,&font_token,throws,tile),
            "text exception propagates as failure");
        require(!draw_song_hud_overlay(anchor,metadata,metrics,nullptr,nullptr,&font_token,text,tile),
            "missing Canvas fails");
        drawn.clear(); int faded_tiles=0; expected_alpha=.5f;
        const OverlayDrawTile faded=[&](void*,float,float,float,float,float,float,float,float,
            const runtime_layouts::FLinearColor* color,const void*,bool) {
            require(color->a==.5f,"artwork/frame follow fresh native opacity"); ++faded_tiles; return true;
        };
        require(draw_song_hud_overlay(anchor,metadata,metrics,&opaque_resource,&canvas_token,&font_token,text,faded,.5f)
            && faded_tiles==1 && !drawn.empty(),"native item foreground fades text together with artwork");
        require(!draw_song_hud_overlay(anchor,metadata,metrics,&opaque_resource,&canvas_token,&font_token,text,faded,
            std::numeric_limits<float>::quiet_NaN()),"nonfinite opacity rejected");
        ff7r::piano::core::ThemeImage image;
        image.width=image.height=256; image.bgra.resize(256*256*4);
        for (std::size_t i=0;i<image.bgra.size();i+=4) {
            image.bgra[i]=40; image.bgra[i+1]=80; image.bgra[i+2]=120; image.bgra[i+3]=255;
        }
        round_song_hud_cover(image);
        require(image.bgra[3]==0 && image.bgra[(255*256+255)*4+3]==0,
            "cover has real transparent round corners");
        const auto center=(128*256+128)*4, edge=128*4;
        require(image.bgra[center]==40 && image.bgra[center+3]==255
            && image.bgra[edge]>200 && image.bgra[edge+3]>0, "original center and fine light curved border");
        bool antialiased=false;
        for (unsigned y=0;y<12;++y) for (unsigned x=0;x<12;++x) {
            const auto alpha=image.bgra[(y*256+x)*4+3];
            antialiased|=alpha>0 && alpha<200;
        }
        require(antialiased,"rounded edge has fractional coverage, not stair-step clipping");
        auto malformed=image; malformed.bgra.resize(4); round_song_hud_cover(malformed);
        require(malformed.bgra.size()==4,"malformed CPU image is not read out of bounds");
        const auto generated=make_song_hud_icons();
        for (const auto& icon:generated) {
            require(icon.width==64 && icon.height==64 && icon.bgra.size()==64*64*4 && !icon.bgra[3],
                "bounded high-resolution icon with transparent outside");
            unsigned ink=0,partial=0;
            for (std::size_t i=3;i<icon.bgra.size();i+=4) {
                ink+=icon.bgra[i]>0; partial+=icon.bgra[i]>0 && icon.bgra[i]<255;
            }
            require(ink>200 && ink<1800 && partial>0,"distinct antialiased outline glyph, not a solid placeholder");
        }
        require(generated[0].bgra!=generated[1].bgra && generated[1].bgra!=generated[2].bgra,
            "icons are distinct metronome, clock and pencil shapes");
        std::puts("song HUD overlay selftest passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
