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
            if (!value) throw std::runtime_error("autoplay configuration mismatch");
        };
        check(ff7r::piano::core::load_config(path.wstring()).autoplay_function_key == 8);
        for (const auto& [value, expected] : {std::pair{L"0", 0}, {L"1", 1},
                 {L"12", 12}, {L"13", 8}, {L"-1", 8}}) {
            check(WritePrivateProfileStringW(L"Autoplay", L"KeyboardFunctionKey", value,
                path.c_str()) != FALSE);
            check(ff7r::piano::core::load_config(path.wstring()).autoplay_function_key == expected);
        }
        std::filesystem::remove(path);
        std::cout << "config selftest passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
