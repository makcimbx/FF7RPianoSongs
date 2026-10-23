#include "game/menu_theme_plan.h"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace plan = ff7r::piano::game::menu_theme_plan;
namespace core = ff7r::piano::core;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
core::ThemeImage pixels() { return {{0, 0, 255, 255}, 1, 1}; }
void file_pipeline_test()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "opencode" / ("theme-plan-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(fs::create_directories(root / "night"), "exclusive file fixture");
    try {
        {
            const std::string png = "89504e470d0a1a0a0000000d4948445200000002000000010806000000f4227f8a"
                "0000000e49444154789c63f8cfc000420d000f7a037e77e97f970000000049454e44ae426082";
            std::ofstream image(root / "night" / "tile.png", std::ios::binary);
            for (std::size_t i = 0; i < png.size(); i += 2)
                image.put(static_cast<char>(std::stoul(png.substr(i, 2), nullptr, 16)));
            image.close(); require(!image.fail(), "write real PNG");
            std::ofstream manifest(root / "night" / "theme.ini", std::ios::binary);
            manifest << "[Theme]\nVersion=1\n[Images]\nMusicList/ListBg=tile.png\n"
                "MusicListCell/MainMenu_Btn.Hovered@selected=tile.png\nSpeedIcon/SpeedIcon.2=tile.png\n"
                "[Colors]\nMusicList/Pnl_Info.ContentColorAndOpacity=#80808080\n"
                "MusicListCell/Txt_Title@selected=#ffffffff\nSpeedIcon/SpeedIcon.2=#ff000080\n";
            manifest.close(); require(!manifest.fail(), "write complete manifest");
        }
        core::ThemeDefinition prepared; plan::Program program; std::string error;
        require(core::load_theme(root, "night", prepared, error), "decode complete manifest");
        require(plan::compile(prepared, program, error) && error.empty(), "compile decoded complete manifest");
        require(program.images.size() == 3 && program.colors.size() == 3 && program.pixel_bytes == 24,
            "all decoded image and palette overrides retained");
        for (const auto& image : program.images)
            require(image.pixels.width == 2 && image.pixels.height == 1
                && image.pixels.bgra == std::vector<std::uint8_t>({0, 0, 255, 255, 255, 0, 0, 128}),
                "WIC straight-alpha BGRA reaches compiled plan unchanged");
        require(std::abs(program.colors.front().linear[0] - 0.2158605f) < 0.000001f,
            "manifest RGB converted once after decoding");
        {
            std::ofstream manifest(root / "night" / "theme.ini", std::ios::binary | std::ios::trunc);
            manifest << "[Theme]\nVersion=1\n[Colors]\nPreview/Background=#80800080\nPreview/Text=#ffffff40\n";
        }
        require(core::load_theme(root, "night", prepared, error) && plan::compile(prepared, program, error)
            && program.images.empty() && program.colors.empty() && program.preview.overrides == 17,
            "real manifest Preview-only roles are value palette, not native targets");
        {
            std::ofstream manifest(root / "night" / "theme.ini", std::ios::binary | std::ios::trunc);
            manifest << "[Theme]\nVersion=1\n[Colors]\nPreview/Text=#ffffff40\nPreview/Text=#000000ff\n";
        }
        require(!core::load_theme(root, "night", prepared, error) && error.find("duplicate") != std::string::npos,
            "real manifest rejects duplicate Preview role, never silently last-wins");
    } catch (...) {
        std::error_code ignored; fs::remove_all(root, ignored); throw;
    }
    fs::remove_all(root);
}
void bundled_examples_test()
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "examples" / "Themes";
    for (const auto* id : {"steins-gate", "evangelion"}) {
        core::ThemeDefinition prepared; plan::Program program; std::string error;
        if (!core::load_theme(root, id, prepared, error)
            || !plan::compile(prepared, program, error))
            throw std::runtime_error(std::string(id) + ": " + error);
        require(program.id == id && !program.images.empty() && !program.colors.empty(),
            "bundled example includes editable images and palette");
        require(program.pixel_bytes < 16 * 1024 * 1024, "bundled example stays small decoded");
        for (const auto& image : program.images)
            require(image.pixels.bgra.size() == std::size_t{image.pixels.width}
                * image.pixels.height * 4, "bundled PNG fully decoded");
    }
}
}
int main()
{
    try {
        plan::Program output; std::string error;
        core::ThemeDefinition source; source.id = "night";
        source.images["MusicList/Bg_1"] = pixels();
        source.images["MusicListCell/MainMenu_Btn.Hovered@selected"] = pixels();
        source.images["SpeedIcon/SpeedIcon.2"] = pixels();
        source.colors["MusicListCell/Txt_Title@normal"] = {128, 10, 255, 128};
        source.colors["MusicListCell/Txt_Rank.OutlineColorAndOpacity@selected"] = {255, 0, 0, 255};
        source.colors["MusicListCell/MainMenu_Btn.Pressed"] = {255, 255, 255, 255};
        require(plan::compile(source, output, error) && error.empty(), "valid complete plan");
        require(output.id == "night" && output.images.size() == 3 && output.colors.size() == 3
            && output.pixel_bytes == 12 && output.images[0].pixels.bgra.size() == 4, "owned payload");
        require(source.images.begin()->second.bgra.empty(), "pixels transferred after validation");
        const auto linear = plan::linear_color({128, 10, 255, 128});
        require(std::abs(linear[0] - 0.2158605f) < 0.000001f
            && std::abs(linear[1] - 0.00303527f) < 0.000001f && linear[2] == 1.0f
            && std::abs(linear[3] - 128.0f / 255.0f) < 0.000001f, "sRGB not alpha conversion");
        const auto reject = [&](const std::string& name, bool color = false) {
            core::ThemeDefinition invalid; invalid.id = "bad";
            invalid.images["MusicList/Bg_1"] = pixels();
            if (color) invalid.colors[name] = {1, 2, 3, 4};
            else invalid.images[name] = pixels();
            require(!plan::compile(invalid, output, error) && !error.empty(), "invalid selector accepted");
            require(output.id == "night" && output.pixel_bytes == 12
                && invalid.images["MusicList/Bg_1"].bgra.size() == 4, "failure atomicity");
        };
        for (const auto* name : {"Unknown/Foo", "MusicList/", "MusicList/Foo/Bar", "MusicList/Bg_1@selected",
            "MusicListCell/Foo@unknown", "MusicListCell/Foo@selected@normal", "MusicList/Foo.Bad",
            "SpeedIcon/SpeedIcon.64", "SpeedIcon/SpeedIcon.-1", "SpeedIcon/SpeedIcon.02",
            "SpeedIcon/SpeedIcon.2x", "MusicList/Bg_1.Brush", "Cursor/Cursor_Left@selected",
            "Cursor/Img_BtnLight@normal"}) reject(name);
        reject("MusicListCell/Txt_Title.BadColor", true);
        reject("MusicListCell/Txt_Title.ForegroundColor", true);
        reject("Preview/Background");
        for (const auto* selector : {"Preview/Unknown", "Preview/Text@normal", "Preview/Text@selected",
            "Preview/Background.ColorAndOpacity", "Preview/Border.BrushTint", "Preview/Accent/Other"}) reject(selector, true);
        core::ThemeDefinition duplicate; duplicate.id = "duplicate";
        duplicate.colors["MusicListCell/Txt_Title"] = {1, 2, 3, 4};
        duplicate.colors["MusicListCell/Txt_Title.ColorAndOpacity"] = {4, 3, 2, 1};
        require(!plan::compile(duplicate, output, error), "normalized palette duplicate");
        core::ThemeDefinition palette; palette.id = "palette-only";
        palette.colors["MusicList/Pnl_Info.ContentColorAndOpacity"] = {255, 128, 0, 192};
        palette.colors["MusicList/Txt_Title_1.ShadowColorAndOpacity"] = {0, 0, 0, 128};
        palette.colors["MusicList/Txt_Title_1.BottomColorAndOpacity"] = {0, 128, 255, 255};
        palette.colors["SpeedIcon/SpeedIcon.0"] = {1, 2, 3, 4};
        require(plan::compile(palette, output, error) && output.images.empty()
            && output.colors.size() == 4 && !output.pixel_bytes, "palette-only and brush tint plan");
        core::ThemeDefinition preview; preview.id = "preview-only";
        preview.colors["Preview/Accent"] = {128, 10, 255, 128};
        require(plan::compile(preview, output, error) && output.images.empty() && output.colors.empty()
            && !output.pixel_bytes && output.preview.overrides == 4, "partial Preview-only plan");
        const plan::PreviewPalette defaults;
        require(output.preview.linear[2] == linear && output.preview.linear[0] == defaults.linear[0]
            && output.preview.linear[1] == defaults.linear[1] && output.preview.linear[3] == defaults.linear[3]
            && output.preview.linear[4] == defaults.linear[4], "independent exact linear defaults, one sRGB conversion including unchanged alpha");
        for (const auto* role : {"Background", "Border", "Track", "Text"}) preview.colors[std::string("Preview/") + role] = {1, 2, 3, 4};
        require(plan::compile(preview, output, error) && output.preview.overrides == 31 && output.colors.empty(), "all five Preview roles");
        core::ThemeDefinition cursor; cursor.id = "cursor-only";
        for (const auto* name : {"Cursor_Left", "Img_BtnLight", "HoverBase"}) {
            cursor.images[std::string("Cursor/") + name] = pixels();
            cursor.colors[std::string("Cursor/") + name] = {128, 10, 255, 128};
        }
        require(plan::compile(cursor, output, error) && output.images.size() == 3
            && output.colors.size() == 3 && output.pixel_bytes == 12, "cursor arrow and glow plan");
        for (const auto& image : output.images)
            require(image.target.scope == plan::Scope::Cursor && image.target.state == plan::State::Any,
                "cursor has its own owner scope, not a song-row state");
        for (const auto& color : output.colors)
            require(color.target.scope == plan::Scope::Cursor && color.linear == linear,
                "cursor palette uses normal linear conversion");
        core::ThemeDefinition many; many.id = "many-colors";
        for (int i = 0; i < 512; ++i)
            many.colors["MusicList/Widget" + std::to_string(i)] = {1, 2, 3, 255};
        require(plan::compile(many, output, error) && output.colors.size() == 512,
            "valid palette rejected by an arbitrary entry count");
        core::ThemeDefinition invalid; invalid.id = "empty";
        require(!plan::compile(invalid, output, error), "empty non-original");
        invalid.id = "original"; invalid.images["MusicList/Bg_1"] = pixels();
        require(!plan::compile(invalid, output, error), "reserved original overrides");
        invalid.id = "oversize"; invalid.images.begin()->second.width = 4097;
        require(!plan::compile(invalid, output, error), "prepared image dimensions");
        core::ThemeDefinition original;
        require(plan::compile(original, output, error) && output.id == "original"
            && output.images.empty() && output.colors.empty() && !output.pixel_bytes
            && output.preview == defaults, "original clears native and Preview plan");
        file_pipeline_test();
        bundled_examples_test();
        std::cout << "PASS theme plan\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
