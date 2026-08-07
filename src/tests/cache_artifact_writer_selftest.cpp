#include "pipeline/cache.h"
#include "pipeline/cache_artifact_writer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(out);
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

int fail(const char* message) {
    std::cerr << "cache_artifact_writer_selftest: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    namespace writer = ff7rp::pipeline::cache_artifact_writer;

    fs::path root;
    std::error_code ec;
    for (std::uint32_t candidate = 0; candidate < 1024 && root.empty(); ++candidate) {
        const fs::path path = fs::temp_directory_path() /
            ("ff7rp_cache_artifact_writer_selftest_" + std::to_string(candidate));
        ec.clear();
        if (fs::create_directory(path, ec)) root = path;
        else if (ec) return fail("failed to claim fixture root");
    }
    if (root.empty()) return fail("no unique fixture root available");

    const fs::path target = root / "success" / "artifact.bin";
    fs::create_directories(target.parent_path(), ec);
    if (ec || !write_bytes(target, {0x01, 0x02}) || !write_bytes(target.string() + ".tmp", {0xff})) {
        return fail("failed to prepare success fixture");
    }
    const std::vector<std::uint8_t> expected{0x10, 0x20, 0x30, 0x40};
    const auto success = writer::write_binary_file(target.string(), expected);
    if (!success.ok() || read_bytes(target) != expected || fs::exists(target.string() + ".tmp")) {
        return fail("atomic success did not replace the target and consume the stale temporary file");
    }

    const fs::path blocked_parent = root / "blocked-parent";
    if (!write_bytes(blocked_parent, {0x55})) return fail("failed to prepare parent obstruction");
    const fs::path blocked_target = blocked_parent / "artifact.bin";
    const auto blocked = writer::write_binary_file(blocked_target.string(), expected);
    std::error_code expected_error;
    fs::create_directories(blocked_parent, expected_error);
    const std::string blocked_message = "failed to create cache directory for " +
        blocked_target.string() + ": " + expected_error.message();
    if (blocked.code != ff7rp::pipeline::StatusCode::IoError || blocked.message != blocked_message ||
        fs::exists(blocked_target) || fs::exists(blocked_target.string() + ".tmp")) {
        return fail("parent creation failure changed status text or published output");
    }

    const fs::path open_target = root / "open-failure.bin";
    const std::vector<std::uint8_t> original{0x77, 0x88};
    if (!write_bytes(open_target, original)) return fail("failed to prepare open failure target");
    fs::create_directory(open_target.string() + ".tmp", ec);
    if (ec) return fail("failed to prepare temporary-path obstruction");
    const auto open_failure = writer::write_binary_file(open_target.string(), expected);
    if (open_failure.code != ff7rp::pipeline::StatusCode::IoError ||
        open_failure.message != "failed to open cache file for writing: " + open_target.string() ||
        read_bytes(open_target) != original || !fs::is_directory(open_target.string() + ".tmp")) {
        return fail("temporary-path open failure changed status, target, or stale obstruction handling");
    }

    const fs::path replace_target = root / "replace-failure";
    fs::create_directory(replace_target, ec);
    if (ec) return fail("failed to prepare replacement obstruction");
    const auto replace_failure = writer::write_binary_file(replace_target.string(), expected);
    if (replace_failure.code != ff7rp::pipeline::StatusCode::IoError ||
        !starts_with(replace_failure.message, "failed to atomically replace cache file: ") ||
        !fs::is_directory(replace_target) || fs::exists(replace_target.string() + ".tmp")) {
        return fail("replacement failure did not preserve target and clean the temporary file");
    }

    const fs::path unreported_target = root / "runtime" / "runtime.bin";
    if (writer::create_parent_directories(unreported_target.string()) ||
        !writer::write_binary_file_unreported(unreported_target.string(), expected) ||
        read_bytes(unreported_target) != expected) {
        return fail("unreported runtime-style publication failed");
    }
    const fs::path unreported_obstruction = root / "runtime-obstruction";
    fs::create_directory(unreported_obstruction, ec);
    const std::error_code obstruction_parent_error =
        writer::create_parent_directories(unreported_obstruction.string());
    if (ec || obstruction_parent_error) {
        return fail("failed to prepare unreported replacement obstruction");
    }
    if (writer::write_binary_file_unreported(unreported_obstruction.string(), expected) ||
        !fs::is_directory(unreported_obstruction) || fs::exists(unreported_obstruction.string() + ".tmp")) {
        return fail("unreported replacement failure changed target or left temporary output");
    }

    const ff7rp::pipeline::Status diagnostic = ff7rp::pipeline::Status::error(
        ff7rp::pipeline::StatusCode::InvalidAudio, "diagnostic text");
    writer::write_last_error(root.string(), diagnostic);
    const fs::path last_error = ff7rp::pipeline::cache_last_error_path(root.string());
    const std::vector<std::uint8_t> expected_diagnostic{
        'd', 'i', 'a', 'g', 'n', 'o', 's', 't', 'i', 'c', ' ', 't', 'e', 'x', 't', '\n'};
    if (read_bytes(last_error) != expected_diagnostic) return fail("direct last-error write bytes changed");
    writer::clear_last_error(root.string());
    if (fs::exists(last_error)) return fail("last-error cleanup did not remove the diagnostic");

    fs::remove_all(root, ec);
    return 0;
}
