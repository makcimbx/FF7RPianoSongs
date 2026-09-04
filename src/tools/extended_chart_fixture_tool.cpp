#include "tools/extended_chart_fixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

class StagingDirectory {
public:
    explicit StagingDirectory(const fs::path& destination)
    {
        static std::atomic<unsigned long long> sequence{0};
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0; attempt < 1024; ++attempt) {
            const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
            const std::string name = "." + destination.filename().string() +
                ".ff7rp-staging-" + std::to_string(stamp) + "-" + std::to_string(id);
            const fs::path candidate = destination.parent_path() / name;
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw fs::filesystem_error("could not create fixture staging directory", candidate, error);
            }
        }
        throw std::runtime_error("could not allocate a unique fixture staging directory");
    }

    ~StagingDirectory()
    {
        if (published_) return;
        std::error_code error;
        fs::remove_all(path_, error);
        if (error) {
            std::cerr << "fixture staging cleanup failed: " << path_.string()
                      << ": " << error.message() << '\n';
        }
    }

    const fs::path& path() const { return path_; }
    void mark_published() { published_ = true; }

private:
    fs::path path_;
    bool published_ = false;
};

template <typename T>
void write_value(std::ofstream& out, const T value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool write_silent_wav(const fs::path& path)
{
    constexpr std::uint32_t kSampleRate = 48000;
    constexpr std::uint32_t kSeconds = 70;
    constexpr std::uint16_t kChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 16;
    constexpr std::uint32_t kDataBytes = kSampleRate * kSeconds * kChannels * (kBitsPerSample / 8);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write("RIFF", 4);
    write_value(out, 36u + kDataBytes);
    out.write("WAVEfmt ", 8);
    write_value(out, 16u);
    write_value(out, static_cast<std::uint16_t>(1));
    write_value(out, kChannels);
    write_value(out, kSampleRate);
    write_value(out, kSampleRate * kChannels * (kBitsPerSample / 8));
    write_value(out, static_cast<std::uint16_t>(kChannels * (kBitsPerSample / 8)));
    write_value(out, kBitsPerSample);
    out.write("data", 4);
    write_value(out, kDataBytes);
    constexpr std::size_t kZeroBlockSize = 8192;
    const char zeros[kZeroBlockSize]{};
    std::uint32_t remaining = kDataBytes;
    while (remaining) {
        const auto count = static_cast<std::streamsize>(std::min<std::uint32_t>(remaining, kZeroBlockSize));
        out.write(zeros, count);
        remaining -= static_cast<std::uint32_t>(count);
    }
    return out.good();
}

fs::path unique_sibling_path(const fs::path& destination, const char* purpose)
{
    static std::atomic<unsigned long long> sequence{0};
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0; attempt < 1024; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        const fs::path candidate = destination.parent_path() /
            ("." + destination.filename().string() + ".ff7rp-" + purpose + "-" +
                std::to_string(stamp) + "-" + std::to_string(id));
        std::error_code error;
        const bool exists = fs::exists(candidate, error);
        if (error) throw fs::filesystem_error("could not inspect fixture sibling path", candidate, error);
        if (!exists) return candidate;
    }
    throw std::runtime_error("could not allocate a unique fixture sibling path");
}

void validate_fixture_directory(const fs::path& directory)
{
    constexpr std::uintmax_t kExpectedWavSize = 44u + 48000u * 70u * 2u;
    std::size_t file_count = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()
            || (entry.path().filename() != "song.json" && entry.path().filename() != "song.wav")) {
            throw std::runtime_error("fixture contains an unexpected entry");
        }
        ++file_count;
    }
    std::ifstream json(directory / "song.json", std::ios::binary);
    std::ostringstream json_bytes;
    json_bytes << json.rdbuf();
    const std::string text = json_bytes.str();
    std::size_t note_count = 0;
    for (std::size_t offset = 0; (offset = text.find("\"pitch\"", offset)) != std::string::npos;
         offset += 7u) {
        ++note_count;
    }
    if (!json.is_open() || file_count != 2u || note_count != 520u
        || text.find("diagnostic_extended_chart_fixture") != std::string::npos
        || fs::file_size(directory / "song.wav") != kExpectedWavSize) {
        throw std::runtime_error("fixture validation failed");
    }
}

std::string rollback_fixture_publication(
    const fs::path& output,
    const fs::path& previous,
    const fs::path& staging,
    bool published,
    bool previous_moved,
    const ff7rp::tools::ExtendedChartFixtureFailure injected_failure)
{
    std::string failures;
    if (published) {
        if (injected_failure ==
            ff7rp::tools::ExtendedChartFixtureFailure::rollback_output_cleanup_failure) {
            failures = "injected generated-fixture cleanup failure; failed fixture retained at " +
                output.string();
        } else {
            std::error_code error;
            fs::remove_all(output, error);
            if (error) {
                failures = "generated fixture cleanup failed at " + output.string() + ": " +
                    error.message() + "; preserved empty destination backup path was " +
                    previous.string();
            }
        }
    }
    if (previous_moved) {
        std::error_code exists_error;
        const bool output_exists = fs::exists(output, exists_error);
        if (exists_error) {
            if (!failures.empty()) failures += "; ";
            failures += "could not inspect fixture output " + output.string() +
                " during rollback; prior empty directory retained at " + previous.string() + ": " +
                exists_error.message();
        } else if (output_exists) {
            if (!failures.empty()) failures += "; ";
            failures += "empty destination could not be restored because the failed or obstructing path " +
                output.string() + " exists; prior empty directory retained at " + previous.string();
        } else {
            std::error_code rename_error;
            fs::rename(previous, output, rename_error);
            if (rename_error) {
                if (!failures.empty()) failures += "; ";
                failures += "empty destination rollback failed; prior directory retained at " +
                    previous.string() + "; restore destination was " + output.string() + ": " +
                    rename_error.message();
            }
        }
    }
    if (!failures.empty() && !staging.empty()) {
        failures += "; fixture staging path was " + staging.string();
    }
    return failures;
}

} // namespace

bool ff7rp::tools::create_extended_chart_fixture(
    const fs::path& destination,
    std::string* error_message,
    const ExtendedChartFixtureFailure injected_failure)
{
    fs::path output;
    fs::path previous;
    fs::path staging_path;
    bool previous_moved = false;
    bool published = false;
    try {
        if (destination.empty()) {
            if (error_message) *error_message = "output path must not be empty";
            return false;
        }
        output = fs::absolute(destination).lexically_normal();
        const fs::path parent = output.parent_path();
        if (output.filename().empty() || !fs::exists(parent) || !fs::is_directory(parent)) {
            if (error_message) *error_message = "output parent must be an existing directory";
            return false;
        }
        if (fs::exists(output) && (!fs::is_directory(output) || !fs::is_empty(output))) {
            if (error_message) *error_message = "output must be new or an existing empty directory";
            return false;
        }

        StagingDirectory staging(output);
        staging_path = staging.path();
        std::ofstream json(staging.path() / "song.json", std::ios::trunc);
        if (!json) throw std::runtime_error("could not create staged song.json");
        json << "{\n"
             << "  \"schema\": \"ff7rpianosongs.song.v2\",\n"
             << "  \"title\": \"Extended Chart Ordinary 520\",\n"
             << "  \"bpm\": 120,\n"
             << "  \"difficulty\": 0,\n"
             << "  \"metronome\": { \"enabled\": false, \"level\": 0.12 },\n"
             << "  \"notes\": [\n";
        for (std::size_t row = 0; row < 520u; ++row) {
            json << "    { \"beat\": " << (row / 4u) << '.' << (row % 4u) * 25u
                 << ", \"duration_beats\": 0.125, \"pitch\": \"C4\" }"
                 << (row + 1u == 520u ? "\n" : ",\n");
        }
        json << "  ]\n}\n";
        json.close();
        if (!json) throw std::runtime_error("failed to finish staged song.json");
        if (injected_failure == ExtendedChartFixtureFailure::after_json) {
            throw std::runtime_error("injected failure after staged song.json");
        }
        if (!write_silent_wav(staging.path() / "song.wav")) {
            throw std::runtime_error("failed to write staged song.wav");
        }
        if (injected_failure == ExtendedChartFixtureFailure::after_wav) {
            throw std::runtime_error("injected failure after staged song.wav");
        }

        validate_fixture_directory(staging.path());
        if (injected_failure == ExtendedChartFixtureFailure::before_publish) {
            throw std::runtime_error("injected failure before fixture publication");
        }

        if (fs::exists(output)) {
            if (!fs::is_directory(output) || !fs::is_empty(output)) {
                if (error_message) *error_message = "output changed while fixture was being generated";
                return false;
            }
            previous = unique_sibling_path(output, "previous");
            fs::rename(output, previous);
            previous_moved = true;
            if (injected_failure == ExtendedChartFixtureFailure::rollback_destination_obstruction) {
                fs::create_directory(output);
                std::ofstream obstruction(output / "injected-obstruction.txt", std::ios::trunc);
                obstruction << "obstruction";
                obstruction.close();
                if (!obstruction) throw std::runtime_error("could not create injected fixture obstruction");
                throw std::runtime_error("injected fixture destination obstruction");
            }
            if (injected_failure == ExtendedChartFixtureFailure::after_destination_preserved) {
                throw std::runtime_error("injected failure after empty destination preservation");
            }
        }
        if (injected_failure == ExtendedChartFixtureFailure::publish_rename) {
            throw std::runtime_error("injected fixture publish rename failure");
        }
        fs::rename(staging.path(), output);
        published = true;
        if (injected_failure == ExtendedChartFixtureFailure::after_publish) {
            throw std::runtime_error("injected failure after fixture publication");
        }
        if (injected_failure == ExtendedChartFixtureFailure::rollback_output_cleanup_failure) {
            throw std::runtime_error("injected failure requiring generated fixture cleanup");
        }
        validate_fixture_directory(output);
        if (previous_moved && !fs::remove(previous)) {
            throw std::runtime_error("could not retire preserved empty destination");
        }
        previous_moved = false;
        staging.mark_published();
        return true;
    } catch (const fs::filesystem_error& error) {
        const std::string rollback = rollback_fixture_publication(
            output, previous, staging_path, published, previous_moved, injected_failure);
        if (error_message) {
            *error_message = "filesystem error: " + std::string(error.what());
            if (!rollback.empty()) *error_message += "; " + rollback;
        }
        return false;
    } catch (const std::exception& error) {
        const std::string rollback = rollback_fixture_publication(
            output, previous, staging_path, published, previous_moved, injected_failure);
        if (error_message) {
            *error_message = error.what();
            if (!rollback.empty()) *error_message += "; " + rollback;
        }
        return false;
    }
}

#ifndef FF7RP_EXTENDED_CHART_FIXTURE_LIBRARY
int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: extended_chart_fixture_tool <empty-output-directory>\n";
        return 2;
    }
    std::string error;
    if (!ff7rp::tools::create_extended_chart_fixture(argv[1], &error)) {
        std::cerr << "failed to create fixture: " << error << '\n';
        return 3;
    }
    std::cout << "created exactly-520 ordinary extended fixture at "
              << fs::absolute(argv[1]).lexically_normal().string() << '\n';
    return 0;
}
#endif
