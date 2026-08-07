#include "tests/documentation_parity.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 4) {
        std::cerr << "usage: release_audit_tool <source-root> [--staged-docs <package-root>]\n";
        return 2;
    }
    if (argc == 4 && std::string(argv[2]) != "--staged-docs") {
        std::cerr << "unknown audit mode: " << argv[2] << '\n';
        return 2;
    }
    std::string error;
    const std::filesystem::path source_root = argv[1];
    const bool ok = argc == 4
        ? ff7rp::tests::verify_staged_documentation_parity(source_root, argv[3], &error)
        : ff7rp::tests::verify_documentation_parity_at(source_root, &error);
    if (!ok) {
        std::cerr << "release audit failed: " << error << '\n';
        return 1;
    }
    ff7rp::tests::ReleaseMetadata metadata;
    if (!ff7rp::tests::load_release_metadata(source_root, &metadata, &error)) {
        std::cerr << "release metadata failed: " << error << '\n';
        return 1;
    }
    ff7rp::tests::DocumentationRegistry registry;
    if (!ff7rp::tests::load_documentation_registry(source_root, &registry, &error)) {
        std::cerr << "documentation registry failed: " << error << '\n';
        return 1;
    }
    std::cout << "release audit ok version=" << metadata.release.version
              << " archive=" << metadata.release.archive_basename
              << " pipeline=" << metadata.pipeline_cache_version
              << " runtime_magic=" << metadata.runtime_cache_magic
              << " runtime_format=" << metadata.runtime_cache_format
              << " ctests=" << metadata.ctest_registrations.size()
              << " hooks=" << metadata.hook_specs.size()
              << '/' << metadata.required_hook_specs
              << '/' << (metadata.hook_specs.size() - metadata.required_hook_specs)
              << " package_docs=" << ff7rp::tests::package_documents(registry).size()
              << " canonical_docs=" << registry.documents.size() << '\n';
    for (const std::string& test : metadata.ctest_registrations) {
        std::cout << "ctest=" << test << '\n';
    }
    for (const std::string& hook : metadata.hook_specs) {
        std::cout << "hook=" << hook << '\n';
    }
    return 0;
}
