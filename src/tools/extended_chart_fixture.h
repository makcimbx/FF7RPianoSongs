#pragma once

#include <filesystem>
#include <string>

namespace ff7rp::tools {

enum class ExtendedChartFixtureFailure {
    none,
    after_json,
    after_wav,
    before_publish,
    after_destination_preserved,
    publish_rename,
    after_publish,
    rollback_output_cleanup_failure,
    rollback_destination_obstruction,
};

bool create_extended_chart_fixture(
    const std::filesystem::path& destination,
    std::string* error_message,
    ExtendedChartFixtureFailure injected_failure = ExtendedChartFixtureFailure::none);

} // namespace ff7rp::tools
