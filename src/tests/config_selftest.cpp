#include "core/config.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

int main()
{
    const auto path = std::filesystem::temp_directory_path()
        / (L"ff7rp-config-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    try {
        const auto check = [](bool value) {
            if (!value) throw std::runtime_error("configuration mismatch");
        };
        check(ff7r::piano::core::load_config(path.wstring()).autoplay_function_key == 8);
        using ff7r::piano::core::PreviewMode;
        const auto defaults = ff7r::piano::core::load_config(path.wstring());
        check(defaults.preview_mode == PreviewMode::Auto && defaults.preview_function_key == 7);
        check(defaults.selected_theme == "original");
        check(defaults.theme_function_key == 6);
        for (const auto& [value, expected] : {std::pair{L"0", 0}, {L"1", 1},
                 {L"12", 12}, {L"13", 8}, {L"-1", 8}}) {
            check(WritePrivateProfileStringW(L"Autoplay", L"KeyboardFunctionKey", value,
                path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).autoplay_function_key == expected);
        }
        for (const auto& [value, expected] : {
                 std::pair{L"auto", PreviewMode::Auto}, {L"MANUAL", PreviewMode::Manual},
                 {L"off", PreviewMode::Off}, {L"invalid", PreviewMode::Auto}}) {
            check(WritePrivateProfileStringW(L"Preview", L"Mode", value, path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).preview_mode == expected);
        }
        for (const auto& [value, expected] : {std::pair{L"0", 0}, {L"1", 1},
                 {L"12", 12}, {L"13", 7}, {L"-1", 7}}) {
            check(WritePrivateProfileStringW(L"Preview", L"KeyboardFunctionKey", value,
                path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).preview_function_key == expected);
        }
        check(ff7r::piano::core::save_selected_theme(path.wstring(), "night-2"));
        check(ff7r::piano::core::load_config(path.wstring()).selected_theme == "night-2");
        check(ff7r::piano::core::load_config(path.wstring()).preview_function_key == 7);
        for (const auto& [value, expected] : {std::pair{L"0", 0}, {L"1", 1},
                 {L"12", 12}, {L"13", 6}, {L"-1", 6}}) {
            check(WritePrivateProfileStringW(L"Theme", L"KeyboardFunctionKey", value,
                path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).theme_function_key == expected);
        }
        check(ff7r::piano::core::load_config(path.wstring()).selected_theme == "night-2");
        check(!ff7r::piano::core::save_selected_theme(path.wstring(), "../outside"));
        check(ff7r::piano::core::load_config(path.wstring()).selected_theme == "night-2");
        for (const auto& value : {L"../outside", L"", L"\x043d\x043e\x0447\x044c"}) {
            check(WritePrivateProfileStringW(L"Theme", L"Selected", value, path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).selected_theme == "original");
        }
        check(ff7r::piano::core::save_selected_theme(path.wstring(), std::string(64, 'x')));
        check(ff7r::piano::core::load_config(path.wstring()).selected_theme == std::string(64, 'x'));
        check(!ff7r::piano::core::save_selected_theme(path.wstring(), std::string(65, 'x')));
        check(!ff7r::piano::core::save_selected_theme((path / "missing.ini").wstring(), "night"));
        std::filesystem::remove(path);
        std::cout << "config selftest passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
