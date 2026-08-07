#include "pipeline/song_repository.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: song_repository_benchmark <song-directory> [...]\n";
        return 2;
    }

    using Clock = std::chrono::steady_clock;
    for (int index = 1; index < argc; ++index) {
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
            return 1;
        }
    }
    return 0;
}
