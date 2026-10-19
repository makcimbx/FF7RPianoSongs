#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ff7rp::transcription {

inline constexpr wchar_t kMuscriptorModelFileName[] = L"muscriptor-small-f16.gguf";

// Makes MuScriptor the pipeline's audio transcriber when model_path exists; returns false otherwise.
// cpu_threads applies only when no discrete GPU is available.
bool register_muscriptor_transcriber(std::filesystem::path model_path, int cpu_threads,
    std::function<void(const std::string&)> log = {});

} // namespace ff7rp::transcription
