#include "game/song_hud_artwork.h"

#include <algorithm>
#include <cmath>

namespace ff7r::piano::game {
namespace {
float segment(float x, float y, float ax, float ay, float bx, float by) noexcept
{
    const float dx=bx-ax, dy=by-ay;
    const float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.0f,1.0f);
    return std::hypot(x-ax-t*dx,y-ay-t*dy)-2;
}
std::uint8_t byte(float value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0f,255.0f)));
}
}

void round_song_hud_cover(core::ThemeImage& image) noexcept
{
    const auto side=std::min(image.width,image.height);
    if (side<16 || image.width>4096 || image.height>4096
        || image.bgra.size()!=static_cast<std::size_t>(image.width)*image.height*4) return;
    const float half_w=image.width*.5f, half_h=image.height*.5f;
    const float radius=side*.035f, border=side*(.75f/110);
    for (std::uint32_t y=0;y<image.height;++y) for (std::uint32_t x=0;x<image.width;++x) {
        const float qx=std::abs(x+.5f-half_w)-(half_w-radius);
        const float qy=std::abs(y+.5f-half_h)-(half_h-radius);
        const float d=std::hypot(std::max(qx,0.0f),std::max(qy,0.0f))
            +std::min(std::max(qx,qy),0.0f)-radius;
        const float outer=std::clamp(.5f-d,0.0f,1.0f);
        const float inner=std::clamp(.5f-d-border,0.0f,1.0f);
        auto* pixel=image.bgra.data()+(static_cast<std::size_t>(y)*image.width+x)*4;
        const float ink=inner*pixel[3]/255, frame=(outer-inner)*.8f, alpha=ink+frame;
        // Keep the border color under transparent pixels to avoid dark bilinear fringes.
        for (unsigned c=0;c<3;++c) pixel[c]=alpha>0 ? byte((pixel[c]*ink+255*frame)/alpha) : 255;
        pixel[3]=byte(alpha*255);
    }
}

std::array<core::ThemeImage,kSongHudIconCount> make_song_hud_icons()
{
    std::array<core::ThemeImage,kSongHudIconCount> images;
    for (std::size_t i=0;i<images.size();++i) {
        auto& image=images[i]; image.width=image.height=64; image.bgra.resize(64*64*4);
        for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
            const float px=x+.5f, py=y+.5f;
            float d=64;
            const auto line=[&](float ax,float ay,float bx,float by) {
                d=std::min(d,segment(px,py,ax,ay,bx,by));
            };
            switch (static_cast<SongHudIcon>(i)) {
            case SongHudIcon::Metronome:
                line(24,9,40,9); line(40,9,49,55); line(49,55,15,55); line(15,55,24,9);
                line(32,46,43,18); line(27,46,37,46); break;
            case SongHudIcon::Clock:
                d=std::abs(std::hypot(px-32,py-32)-23)-2;
                line(32,32,32,17); line(32,32,43,39); break;
            case SongHudIcon::Pencil:
                line(13,44,42,15); line(42,15,50,23); line(50,23,21,52);
                line(21,52,11,54); line(11,54,13,44); line(38,19,46,27);
                line(16,42,24,50); break;
            default: break;
            }
            auto* pixel=image.bgra.data()+(y*64+x)*4;
            pixel[0]=pixel[1]=pixel[2]=255;
            pixel[3]=byte(std::clamp(.5f-d,0.0f,1.0f)*255);
        }
    }
    return images;
}
} // namespace ff7r::piano::game
