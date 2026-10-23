#include "core/theme.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ff7r::piano::core {
namespace {

constexpr std::size_t kManifestLimit = 64 * 1024;
constexpr std::size_t kImageFileLimit = 32 * 1024 * 1024;
constexpr std::uint64_t kPixelByteLimit = 128 * 1024 * 1024;
constexpr std::uint32_t kDimensionLimit = 4096;

bool identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path, std::size_t limit)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open theme file");
    const auto size = stream.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(limit))
        throw std::runtime_error("theme file is empty or exceeds size limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))
        || stream.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("cannot read stable theme file");
    return bytes;
}

std::filesystem::path contained_file(const std::filesystem::path& directory,
    const std::filesystem::path& relative)
{
    if (relative.empty() || relative.has_root_path())
        throw std::runtime_error("theme path must be relative");
    for (const auto& part : relative) {
        if (part == "..") throw std::runtime_error("theme path traversal rejected");
    }
    const auto path = std::filesystem::canonical(directory / relative);
    const auto suffix = path.lexically_relative(directory);
    if (suffix.empty() || *suffix.begin() == ".." || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("theme file escapes its directory or is not a file");
    return path;
}

std::uint32_t big_endian(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (std::uint32_t{bytes[offset]} << 24) | (std::uint32_t{bytes[offset + 1]} << 16)
        | (std::uint32_t{bytes[offset + 2]} << 8) | bytes[offset + 3];
}

void check_wic(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("PNG decode failed: WIC HRESULT " + std::to_string(result));
}

void decode_pixels(std::vector<std::uint8_t>& png, ThemeImage& image)
{
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (apartment.result != RPC_E_CHANGED_MODE) check_wic(apartment.result);
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    check_wic(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IWICStream> stream;
    check_wic(factory->CreateStream(stream.GetAddressOf()));
    check_wic(stream->InitializeFromMemory(png.data(), static_cast<DWORD>(png.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    check_wic(CoCreateInstance(CLSID_WICPngDecoder, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(decoder.GetAddressOf())));
    check_wic(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnLoad));
    ComPtr<IWICBitmapFrameDecode> frame;
    check_wic(decoder->GetFrame(0, frame.GetAddressOf()));
    UINT width = 0, height = 0;
    check_wic(frame->GetSize(&width, &height));
    if (width != image.width || height != image.height)
        throw std::runtime_error("PNG decoder dimensions disagree with header");
    ComPtr<IWICFormatConverter> converter;
    check_wic(factory->CreateFormatConverter(converter.GetAddressOf()));
    check_wic(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    image.bgra.resize(static_cast<std::size_t>(width) * height * 4);
    check_wic(converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(image.bgra.size()), image.bgra.data()));
}

ThemeImage read_image(const std::filesystem::path& path, std::uint64_t& pixel_bytes,
    std::uint64_t& file_bytes)
{
    ThemeImage image;
    auto png = read_file(path, kImageFileLimit);
    file_bytes += png.size();
    if (file_bytes > kPixelByteLimit) throw std::runtime_error("theme exceeds encoded image budget");
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (png.size() < 33 || !std::equal(signature.begin(), signature.end(), png.begin())
        || big_endian(png, 8) != 13 || big_endian(png, 12) != 0x49484452)
        throw std::runtime_error("theme image must have a PNG IHDR header");
    image.width = big_endian(png, 16);
    image.height = big_endian(png, 20);
    const auto depth = png[24], color = png[25];
    const bool ordinary = (depth == 8 || depth == 16) && (color == 0 || color == 2 || color == 4 || color == 6);
    const bool indexed = color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8);
    const bool grayscale = color == 0 && (depth == 1 || depth == 2 || depth == 4);
    if (!image.width || !image.height || image.width > kDimensionLimit || image.height > kDimensionLimit
        || !(ordinary || indexed || grayscale)
        || png[26] != 0 || png[27] != 0 || png[28] > 1)
        throw std::runtime_error("invalid PNG format or dimensions");
    pixel_bytes += std::uint64_t{image.width} * image.height * (depth == 16 ? 8 : 4);
    if (pixel_bytes > kPixelByteLimit) throw std::runtime_error("theme exceeds decoded pixel budget");
    decode_pixels(png, image);
    return image;
}

std::array<std::uint8_t, 4> read_color(std::string_view value)
{
    if (value.size() != 9 || value[0] != '#') throw std::runtime_error("color must be #RRGGBBAA");
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("invalid hexadecimal color");
    };
    std::array<std::uint8_t, 4> color{};
    for (std::size_t i = 0; i < color.size(); ++i)
        color[i] = static_cast<std::uint8_t>(nibble(value[1 + i * 2]) * 16 + nibble(value[2 + i * 2]));
    return color;
}

} // namespace

bool valid_theme_id(std::string_view id) noexcept
{
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), identifier_char);
}

bool load_theme(const std::filesystem::path& themes_directory, std::string_view id,
    ThemeDefinition& output, std::string& error)
{
    std::size_t line_number = 0;
    try {
        if (!valid_theme_id(id)) throw std::runtime_error("invalid theme id");
        ThemeDefinition candidate;
        candidate.id = id;
        if (id != "original") {
            const auto root = std::filesystem::canonical(themes_directory);
            const auto directory = std::filesystem::canonical(root / std::string(id));
            if (directory.parent_path() != root) throw std::runtime_error("theme directory escapes theme root");
            const auto bytes = read_file(contained_file(directory, "theme.ini"), kManifestLimit);
            std::string text(bytes.begin(), bytes.end());
            if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
            if (text.find('\0') != std::string::npos) throw std::runtime_error("NUL in theme manifest");
            std::istringstream stream(text);
            std::string line, section;
            bool version_seen = false;
            std::uint64_t pixel_bytes = 0, file_bytes = 0;
            while (std::getline(stream, line)) {
                ++line_number;
                line = trim(line);
                if (line.empty() || line[0] == ';') continue;
                if (line[0] == '[' && line.back() == ']') {
                    section = line.substr(1, line.size() - 2);
                    if (section != "Theme" && section != "Images" && section != "Colors")
                        throw std::runtime_error("unknown theme section");
                    continue;
                }
                const auto equals = line.find('=');
                if (equals == std::string::npos) throw std::runtime_error("expected theme key=value");
                const auto key = trim(line.substr(0, equals));
                const auto value = trim(line.substr(equals + 1));
                if (section == "Theme") {
                    if (key != "Version" || value != "1" || version_seen)
                        throw std::runtime_error("expected one theme Version=1");
                    version_seen = true;
                    continue;
                }
                if (section != "Images" && section != "Colors") throw std::runtime_error("missing theme section");
                if (key.empty() || key.size() > 192 || !std::all_of(key.begin(), key.end(), [](char c) {
                    return identifier_char(c) || c == '/' || c == '.' || c == '@';
                })) throw std::runtime_error("invalid theme target");
                if (section == "Images") {
                    if (candidate.images.contains(key)) throw std::runtime_error("duplicate theme image target");
                    candidate.images.emplace(key, read_image(contained_file(directory,
                        std::filesystem::path(std::u8string(value.begin(), value.end()))), pixel_bytes, file_bytes));
                } else {
                    if (!candidate.colors.emplace(key, read_color(value)).second)
                        throw std::runtime_error("duplicate theme color target");
                }
            }
            if (!version_seen) throw std::runtime_error("missing theme Version=1");
        }
        output = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = (line_number ? "theme.ini:" + std::to_string(line_number) + ": " : "")
            + std::string(exception.what());
        return false;
    }
}

} // namespace ff7r::piano::core
