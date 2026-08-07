#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ff7rp::tests {

struct CanonicalDocument {
    std::string role;
    std::string distribution;
    std::filesystem::path source;
    std::filesystem::path destination;
};

struct DocumentationRegistry {
    std::vector<CanonicalDocument> documents;
};

struct ReleaseAuthority {
    std::string product;
    std::string version;
    std::string platform;
    std::string license;
    std::string supported_executable_catalog_id;
    std::string archive_basename;
};

struct ReleaseMetadata {
    ReleaseAuthority release;
    std::string pipeline_cache_version;
    std::string runtime_cache_magic;
    unsigned int runtime_cache_format = 0;
    std::vector<std::string> ctest_registrations;
    std::vector<std::string> hook_specs;
    std::size_t required_hook_specs = 0;
};

bool parse_release_authority(
    const std::string& json,
    ReleaseAuthority* release,
    std::string* error_message);
bool load_release_authority(
    const std::filesystem::path& source_root,
    ReleaseAuthority* release,
    std::string* error_message);

bool parse_documentation_registry(
    const std::string& json,
    DocumentationRegistry* registry,
    std::string* error_message);
bool load_documentation_registry(
    const std::filesystem::path& source_root,
    DocumentationRegistry* registry,
    std::string* error_message);
std::vector<CanonicalDocument> package_documents(const DocumentationRegistry& registry);
bool load_release_metadata(
    const std::filesystem::path& source_root,
    ReleaseMetadata* metadata,
    std::string* error_message);
bool verify_documentation_parity_at(
    const std::filesystem::path& source_root,
    std::string* error_message);
bool verify_staged_documentation_parity(
    const std::filesystem::path& source_root,
    const std::filesystem::path& package_root,
    std::string* error_message);
bool verify_documentation_parity(std::string* error_message);

} // namespace ff7rp::tests
