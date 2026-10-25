#include "core/theme.h"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace ff7r::piano::core;

namespace {
void check(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    check(static_cast<bool>(stream), "fixture write failed");
}

std::string from_hex(std::string_view hex)
{
    check(hex.size() % 2 == 0, "bad fixture hex");
    std::string bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        bytes += static_cast<char>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
    return bytes;
}
} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path()
        / (L"ff7rp-theme-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root / "night");
        // Two pixels: opaque red, then blue with straight alpha 128.
        const auto png = from_hex("89504e470d0a1a0a0000000d4948445200000002000000010806000000f4227f8a"
            "0000000e49444154789c63f8cfc000420d000f7a037e77e97f970000000049454e44ae426082");
        const std::vector<std::uint8_t> expected{0,0,255,255,255,0,0,128};
        const auto image = root / "night" / "background.png";
        const auto manifest = root / "night" / "theme.ini";
        write(image, png);
        const std::string version = "[Theme]\nVersion=1\n";
        const std::string valid = version + "[Images]\nMusicList/Bg_1=background.png\n"
            "[Colors]\nMusicList/Txt_Title_1=#AABBCCFF\nMusicListCell/Txt_Title@selected=#01234567\n";
        ThemeDefinition theme;
        std::string error;
        check(load_theme(root / "missing", "original", theme, error), "original needs no files");
        write(manifest, "\xef\xbb\xbf" + valid);
        check(load_theme(root, "night", theme, error), "valid theme rejected");
        check(error.empty() && theme.id == "night" && theme.images.size() == 1, "theme projection mismatch");
        check(theme.images.at("MusicList/Bg_1").width == 2
            && theme.images.at("MusicList/Bg_1").height == 1, "PNG bounds mismatch");
        check(theme.colors.at("MusicList/Txt_Title_1") == std::array<std::uint8_t,4>{170,187,204,255}
            && theme.colors.at("MusicListCell/Txt_Title@selected")[3] == 103, "RGBA mismatch");
        write(image, "changed after preparation");
        check(theme.images.at("MusicList/Bg_1").bgra == expected, "owned pixels/straight alpha mismatch");
        write(image, png);
        ThemeImage thumbnail;
        check(load_png_thumbnail(root / "night", "background.png", thumbnail, error)
            && thumbnail.width == 2 && thumbnail.height == 1 && thumbnail.bgra == expected,
            "thumbnail changed small PNG/straight alpha");
        check(!load_png_thumbnail(root / "night", "../outside.png", thumbnail, error)
            && thumbnail.bgra == expected && !error.empty(), "thumbnail traversal/transaction failed");
        check(!load_png_thumbnail(root / "night", "missing.png", thumbnail, error)
            && thumbnail.bgra == expected, "thumbnail missing-file transaction failed");
        const std::u8string unicode_name = u8"\u0444\u043e\u043d.png";
        write(root / "night" / std::filesystem::path(unicode_name), png);
        auto unicode_manifest = valid;
        unicode_manifest.replace(unicode_manifest.find("background.png"), std::string("background.png").size(),
            std::string(unicode_name.begin(), unicode_name.end()));
        write(manifest, unicode_manifest);
        check(load_theme(root, "night", theme, error), "UTF-8 image path rejected");
        // RGB8, RGBA16, indexed1 with transparency, grayscale4.
        for (const std::string_view variant : {
            "89504e470d0a1a0a0000000d49484452000000020000000108020000007b40e8dd0000000d49444154789c63f8cf0004ff01070001ffe2239e590000000049454e44ae426082",
            "89504e470d0a1a0a0000000d4948445200000002000000011006000000a4b2a3c90000001149444154789c63f8ff9f010860644303003a5806fb6cabe5180000000049454e44ae426082",
            "89504e470d0a1a0a0000000d4948445200000002000000010103000000ceecedc900000006504c5445ff00000000ff6ca1fd8e0000000274524e53ff80080fb36a0000000a49444154789c63700000004200412937f4ef0000000049454e44ae426082",
            "89504e470d0a1a0a0000000d494844520000000200000001040000000014b9cd570000000a49444154789c63e0070000110010a96c43e00000000049454e44ae426082"}) {
            write(image, from_hex(variant));
            write(manifest, valid);
            check(load_theme(root, "night", theme, error), "valid PNG format rejected");
            check(theme.images.at("MusicList/Bg_1").bgra.size() == expected.size(), "decoded pixel size mismatch");
        }
        write(image, png);
        check(load_theme(root, "night", theme, error), "restored PNG rejected");
        int rejection_case = 0;
        const auto rejected = [&](const std::string& text) {
            ++rejection_case;
            write(manifest, text);
            const auto saved = theme.images.at("MusicList/Bg_1").bgra;
            if (load_theme(root, "night", theme, error))
                throw std::runtime_error("invalid theme accepted at rejection case " + std::to_string(rejection_case));
            check(!error.empty(), "rejected theme has no diagnostic");
            check(theme.id == "night" && theme.images.at("MusicList/Bg_1").bgra == saved
                && theme.colors.size() == 2, "failure partially published a theme");
        };
        rejected("[Colors]\nText=#12345678\n");
        rejected("[Theme]\nVersion=2\n");
        rejected(version + "Version=1\n");
        rejected(version + "[Unknown]\nKey=Value\n");
        rejected(version + "[Colors]\nText=#123456\n");
        rejected(version + "[Colors]\nText=#1234567Z\n");
        rejected(version + "[Colors]\nText=#12345678\nText=#FFFFFFFF\n");
        rejected(version + "[Colors]\nBad Target=#12345678\n");
        rejected(version + "[Images]\nBg=background.png\nBg=background.png\n");
        rejected(version + "[Images]\nBg=../outside.png\n");
        rejected(version + "[Images]\nBg=C:\\outside.png\n");
        rejected(version + "[Images]\nBg=missing.png\n");
        rejected(version + std::string(64 * 1024, ' '));
        rejected(version + std::string("[Colors]\nBad=\0", 14));
        std::string many = version + "[Colors]\n";
        for (int i = 0; i < 512; ++i) many += "Text" + std::to_string(i) + "=#12345678\n";
        write(manifest, many);
        ThemeDefinition many_colors;
        check(load_theme(root, "night", many_colors, error) && many_colors.colors.size() == 512,
            "valid manifest rejected by an arbitrary entry count");
        write(image, "not a PNG");
        rejected(valid);
        write(image, png.substr(0, 33));
        rejected(valid);
        auto invalid_png = png;
        invalid_png[19] = 0;
        write(image, invalid_png);
        rejected(valid);
        invalid_png = png;
        invalid_png[16] = 0x7f;
        write(image, invalid_png);
        rejected(valid);
        invalid_png = png;
        invalid_png[24] = 3;
        write(image, invalid_png);
        rejected(valid);
        const auto large = from_hex("89504e470d0a1a0a0000000d4948445200000200000002000100000000dc03e957"
            "0000003649444154789cedc101010000008220ffaf6e484001000000000000000000000000000000000000"
            "000000000000000000000000000000007c1b82000001d27889f00000000049454e44ae426082");
        write(image, large);
        check(load_png_thumbnail(root / "night", "background.png", thumbnail, error)
            && thumbnail.width == 256 && thumbnail.height == 256
            && thumbnail.bgra.size() == 256 * 256 * 4, "large thumbnail was not bounded");
        std::string over_budget = version + "[Images]\n";
        for (int i = 0; i < 129; ++i) over_budget += "Bg" + std::to_string(i) + "=background.png\n";
        rejected(over_budget);
        check(error.find("pixel budget") != std::string::npos, "pixel budget fixture failed before budget gate");
        write(image, std::string(32 * 1024 * 1024 + 1, 'x'));
        rejected(valid);
        check(!valid_theme_id("") && !valid_theme_id("../night")
            && !valid_theme_id(std::string(65, 'a')) && valid_theme_id("night-2_test"), "theme ID validation failed");
        check(!load_theme(root, "../night", theme, error), "invalid theme ID loaded");
        check(load_theme(root, "original", theme, error) && error.empty()
            && theme.images.empty() && theme.colors.empty(), "original did not clear overrides");
        std::filesystem::remove_all(root);
        std::cout << "theme selftest passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
