#include "pipeline/song_repository.h"
#include "tests/target_native_assets.h"
#include "pipeline/pipeline_limits.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: song_repository_benchmark [--physical] <song-directory> [...]\n";
        return 2;
    }

    using Clock = std::chrono::steady_clock;
    int first_directory = 1;
    if (std::string(argv[1]) == "--physical") {
        ff7rp::pipeline::configure_chart_row_limit(true, true);
        first_directory = 2;
    }
    if (first_directory == argc) return 2;
    for (int index = first_directory; index < argc; ++index) {
        const std::string directory = argv[index];
        const std::string name = std::filesystem::path(directory).filename().string();
        auto previous = Clock::now();
        ff7rp::pipeline::LoadedSong song;
        const auto trace = [&](const char* stage) {
            const auto now = Clock::now();
            const double milliseconds = std::chrono::duration<double, std::milli>(now - previous).count();
            std::cout << name << '\t' << stage << '\t' << std::fixed << std::setprecision(3)
                      << milliseconds << '\n';
            previous = now;
        };
        const ff7rp::pipeline::Status status =
            ff7rp::pipeline::load_song_directory(directory, &song, trace);
        if (!status.ok()) {
            std::cerr << name << ": " << status.message << '\n';
            for (const auto& omission : song.difficulty_profile_omissions) {
                std::cerr << "  difficulty=" << omission.difficulty
                           << " reason=" << omission.reason
                           << " rows=" << omission.diagnostics.scheduled_rows
                           << " actions=" << omission.diagnostics.selected_actions
                           << " target=" << omission.diagnostics.target_rows
                           << " band=" << omission.diagnostics.target_minimum_rows << '-'
                           << omission.diagnostics.target_maximum_rows
                           << " route=" << omission.diagnostics.satisfied_route_name
                           << " ratio=" << omission.diagnostics.satisfied_route_ratio
                           << " dominant_skill=" << omission.diagnostics.dominant_skill << '\n';
            }
            return 1;
        }
        if (first_directory == 2) {
            for (const auto& profile : song.difficulty_profiles) {
                std::size_t rows = profile.config.notes.size();
                std::size_t groups = 0;
                std::uint8_t previous_group = 0;
                for (const auto& note : profile.config.notes) {
                    if (note.group_index != 0 && note.group_index != previous_group) ++groups;
                    previous_group = note.group_index;
                }
                for (const auto& tail : profile.diagnostic_chart.tail_rows) {
                    const auto& note = tail.source;
                    if (note.group_index != 0 && note.group_index != previous_group) ++groups;
                    previous_group = note.group_index;
                    ++rows;
                }
                std::cout << name << "\tprofile=" << profile.config.difficulty
                          << " rows=" << rows
                          << " events=" << rows
                          << " actions=" << profile.diagnostics.selected_actions
                          << " groups=" << groups
                          << " route=" << profile.diagnostics.satisfied_route_name
                          << " ratio=" << profile.diagnostics.satisfied_route_ratio << '\n';
            }
            for (const auto& omission : song.difficulty_profile_omissions) {
                std::cout << name << "\tomitted=" << omission.difficulty
                          << " reason=" << omission.reason << '\n';
            }
        }
    }
    return 0;
}
