#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ff7rp::tests {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& prefix)
    {
        static std::atomic<unsigned long long> sequence{0};
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const std::filesystem::path parent = std::filesystem::temp_directory_path();
        for (unsigned int attempt = 0; attempt < 1024; ++attempt) {
            const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
            const std::filesystem::path candidate = parent /
                (prefix + "-" + std::to_string(stamp) + "-" + std::to_string(id));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error(
                    "could not create unique test directory", candidate, error);
            }
        }
        throw std::runtime_error("could not allocate a globally unique test directory");
    }

    ~TemporaryDirectory()
    {
        try {
            std::string error;
            if (!cleanup(&error)) {
                std::cerr << "test temporary-directory cleanup failed: " << error << '\n';
            }
        } catch (...) {
            std::cerr << "test temporary-directory cleanup raised an exception\n";
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

    bool cleanup(std::string* error_message = nullptr)
    {
        if (path_.empty()) return true;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error) {
            if (error_message) {
                *error_message = path_.string() + ": " + error.message();
            }
            return false;
        }
        path_.clear();
        return true;
    }

private:
    std::filesystem::path path_;
};

} // namespace ff7rp::tests
