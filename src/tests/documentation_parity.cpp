#include "tests/documentation_parity.h"

#include "tests/target_native_assets.h"
#include "ctest_inventory.generated.h"
#include "game/generated/rvas.generated.h"
#include "game/hook_specs.h"
#include "pipeline/cache.h"
#include "pipeline/chart_compiler.h"
#include "pipeline/native_chord_constituents.h"
#include "pipeline/note_value.h"
#include "pipeline/song_json_fields.h"
#include "pipeline/song_json.h"
#include "pipeline/song_repository.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace ff7rp::tests {
namespace {

bool fail(std::string* error_message, std::string message);

constexpr const char* kRegistrySchema = "ff7rpianosongs.package-docs.v2";

struct RoleDefinition {
    const char* role;
    const char* distribution;
};

constexpr std::array<RoleDefinition, 21> kRequiredRoles{{
    {"entrypoint", "package"},
    {"first-party-license", "package"},
    {"third-party-notices", "package"},
    {"song-format", "package"},
    {"changelog", "package"},
    {"in-game-validation", "repository"},
    {"release-status", "repository"},
    {"docs-index", "repository"},
    {"architecture", "repository"},
    {"build-release", "repository"},
    {"hca-criteria", "repository"},
    {"midi-methodology", "repository"},
    {"chart-limits", "repository"},
    {"analysis-index", "repository"},
    {"audio-lifecycle-evidence", "repository"},
    {"midi-calibration-evidence", "repository"},
    {"chart-row-limit-evidence", "repository"},
    {"cache-optimization-evidence", "repository"},
    {"adaptive-mabf-evidence", "repository"},
    {"chart-event-abi-evidence", "repository"},
    {"developer-tools", "repository"},
}};

template <std::size_t Size>
bool require_documented_tokens(const std::string_view text,
    const std::array<std::string_view, Size>& tokens, const std::string_view category,
    std::string* error_message)
{
    for (const std::string_view token : tokens) {
        const std::string rendered = "`" + std::string(token) + "`";
        if (text.find(rendered) == std::string_view::npos) {
            return fail(error_message, "SongFormat is missing documented " +
                std::string(category) + " token: " + std::string(token));
        }
    }
    return true;
}

class ReleaseJsonParser {
public:
    explicit ReleaseJsonParser(const std::string& input) : input_(input) {}

    bool parse(
        std::map<std::string, std::string>* scalars, std::vector<ReleaseTarget>* targets,
        std::string* error)
    {
        skip_ws();
        if (!consume('{')) return set_error(error, "expected top-level object");
        skip_ws();
        if (consume('}')) return set_error(error, "top-level object is empty");
        std::set<std::string> keys;
        while (true) {
            std::string key;
            if (!parse_string(&key, error)) return false;
            if (!keys.insert(key).second) return set_error(error, "duplicate key '" + key + "'");
            skip_ws();
            if (!consume(':')) return set_error(error, "expected ':' after key");
            if (key == "targets") {
                if (!parse_targets(targets, error)) return false;
            } else {
                std::string value;
                if (!parse_string(&value, error)) return false;
                (*scalars)[key] = std::move(value);
            }
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) return set_error(error, "expected ',' or '}'");
            skip_ws();
        }
        skip_ws();
        if (position_ != input_.size()) return set_error(error, "unexpected trailing data");
        return keys.count("targets") != 0u || set_error(error, "release.json requires a targets array");
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;

    bool parse_targets(std::vector<ReleaseTarget>* targets, std::string* error)
    {
        skip_ws();
        if (!consume('[')) return set_error(error, "targets must be an array");
        skip_ws();
        if (consume(']')) return set_error(error, "targets must not be empty");
        while (true) {
            ReleaseTarget target;
            if (!parse_target(&target, error)) return false;
            targets->push_back(std::move(target));
            skip_ws();
            if (consume(']')) return true;
            if (!consume(',')) return set_error(error, "expected ',' or ']' in targets");
            skip_ws();
        }
    }

    bool parse_target(ReleaseTarget* target, std::string* error)
    {
        skip_ws();
        if (!consume('{')) return set_error(error, "release target must be an object");
        std::set<std::string> keys;
        skip_ws();
        if (consume('}')) return set_error(error, "release target object is empty");
        while (true) {
            std::string key;
            std::string value;
            if (!parse_string(&key, error)) return false;
            if (!keys.insert(key).second) return set_error(error, "duplicate target key '" + key + "'");
            skip_ws();
            if (!consume(':')) return set_error(error, "expected ':' after target key");
            if (!parse_string(&value, error)) return false;
            if (key == "game_build") target->game_build = std::move(value);
            else if (key == "supported_executable_catalog_id") {
                target->supported_executable_catalog_id = std::move(value);
            } else if (key == "archive_basename") target->archive_basename = std::move(value);
            else return set_error(error, "unknown release target key '" + key + "'");
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) return set_error(error, "expected ',' or '}' in release target");
            skip_ws();
        }
        const std::set<std::string> required{
            "game_build", "supported_executable_catalog_id", "archive_basename"};
        return keys == required || set_error(error, "release target does not match the closed field set");
    }

    void skip_ws()
    {
        while (position_ < input_.size()
            && (input_[position_] == ' ' || input_[position_] == '\t'
                || input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
    }
    bool consume(char expected)
    {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }
    bool set_error(std::string* error, const std::string& message) const
    {
        if (error) *error = "release JSON error at byte " + std::to_string(position_) + ": " + message;
        return false;
    }
    bool parse_string(std::string* output, std::string* error)
    {
        skip_ws();
        if (!consume('"')) return set_error(error, "expected string");
        output->clear();
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') return true;
            if (static_cast<unsigned char>(value) < 0x20u) return set_error(error, "control character in string");
            if (value == '\\') {
                if (position_ >= input_.size()) return set_error(error, "unterminated escape");
                const char escaped = input_[position_++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') output->push_back(escaped);
                else return set_error(error, "unsupported escape");
            } else {
                output->push_back(value);
            }
        }
        return set_error(error, "unterminated string");
    }
};

bool fail(std::string* error_message, std::string message)
{
    if (error_message) *error_message = std::move(message);
    return false;
}

bool read_bytes(const fs::path& path, std::string* bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *bytes = buffer.str();
    return input.good() || input.eof();
}

std::vector<std::string> regex_captures(const std::string& text, const std::regex& expression)
{
    std::vector<std::string> values;
    for (std::sregex_iterator match(text.begin(), text.end(), expression), end; match != end; ++match) {
        values.push_back((*match)[1].str());
    }
    return values;
}

bool contains_once(
    const std::string& text, const std::regex& expression, std::string* capture,
    const char* description, std::string* error_message)
{
    const std::vector<std::string> values = regex_captures(text, expression);
    if (values.size() != 1u) {
        return fail(error_message,
            std::string("expected exactly one ") + description + ", found " +
            std::to_string(values.size()));
    }
    *capture = values.front();
    return true;
}

bool is_normalized_relative_path(const fs::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    const std::string generic = path.generic_string();
    if (generic.empty() || generic.find('\\') != std::string::npos) return false;
    for (const fs::path& component : path) {
        if (component == ".." || component == ".") return false;
    }
    return path.lexically_normal().generic_string() == generic;
}

class RegistryJsonParser {
public:
    explicit RegistryJsonParser(const std::string& input) : input_(input) {}

    bool parse(std::string* schema, std::vector<CanonicalDocument>* documents, std::string* error)
    {
        skip_ws();
        if (!consume('{')) return set_error(error, "expected top-level object");
        std::set<std::string> keys;
        skip_ws();
        if (consume('}')) return set_error(error, "top-level object is empty");
        while (true) {
            std::string key;
            if (!parse_string(&key, error)) return false;
            if (!keys.insert(key).second) return set_error(error, "duplicate top-level key '" + key + "'");
            skip_ws();
            if (!consume(':')) return set_error(error, "expected ':' after top-level key");
            if (key == "schema") {
                skip_ws();
                if (!parse_string(schema, error)) return false;
            } else if (key == "documents") {
                if (!parse_documents(documents, error)) return false;
            } else {
                return set_error(error, "unknown top-level key '" + key + "'");
            }
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) return set_error(error, "expected ',' or '}' in top-level object");
            skip_ws();
        }
        skip_ws();
        if (position_ != input_.size()) return set_error(error, "unexpected trailing data");
        if (keys.count("schema") == 0u || keys.count("documents") == 0u) {
            return set_error(error, "top-level object requires schema and documents");
        }
        return true;
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;

    void skip_ws()
    {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++position_;
        }
    }

    bool consume(const char expected)
    {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool set_error(std::string* error, const std::string& message) const
    {
        if (error) {
            *error = "registry JSON error at byte " + std::to_string(position_) + ": " + message;
        }
        return false;
    }

    bool parse_documents(std::vector<CanonicalDocument>* documents, std::string* error)
    {
        skip_ws();
        if (!consume('[')) return set_error(error, "documents must be an array");
        skip_ws();
        if (consume(']')) return true;
        while (true) {
            CanonicalDocument document;
            if (!parse_document(&document, error)) return false;
            documents->push_back(std::move(document));
            skip_ws();
            if (consume(']')) return true;
            if (!consume(',')) return set_error(error, "expected ',' or ']' in documents");
            skip_ws();
        }
    }

    bool parse_document(CanonicalDocument* document, std::string* error)
    {
        if (!consume('{')) return set_error(error, "document must be an object");
        std::set<std::string> keys;
        skip_ws();
        if (consume('}')) return set_error(error, "document object is empty");
        while (true) {
            std::string key;
            std::string value;
            if (!parse_string(&key, error)) return false;
            if (!keys.insert(key).second) return set_error(error, "duplicate document key '" + key + "'");
            if (key != "role" && key != "distribution" && key != "source" && key != "destination") {
                return set_error(error, "unknown document key '" + key + "'");
            }
            skip_ws();
            if (!consume(':')) return set_error(error, "expected ':' after document key");
            skip_ws();
            if (!parse_string(&value, error)) return false;
            if (key == "role") document->role = std::move(value);
            else if (key == "distribution") document->distribution = std::move(value);
            else if (key == "source") document->source = fs::path(value);
            else document->destination = fs::path(value);
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) return set_error(error, "expected ',' or '}' in document object");
            skip_ws();
        }
        for (const char* required : {"role", "distribution", "source"}) {
            if (keys.count(required) == 0u) return set_error(error, std::string("missing document key '") + required + "'");
        }
        return true;
    }

    bool parse_string(std::string* output, std::string* error)
    {
        skip_ws();
        if (!consume('"')) return set_error(error, "expected string");
        output->clear();
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') return true;
            if (static_cast<unsigned char>(value) < 0x20u) return set_error(error, "control character in string");
            if (value != '\\') {
                output->push_back(value);
                continue;
            }
            if (position_ >= input_.size()) return set_error(error, "unterminated escape sequence");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output->push_back('"'); break;
            case '\\': output->push_back('\\'); break;
            case '/': output->push_back('/'); break;
            case 'b': output->push_back('\b'); break;
            case 'f': output->push_back('\f'); break;
            case 'n': output->push_back('\n'); break;
            case 'r': output->push_back('\r'); break;
            case 't': output->push_back('\t'); break;
            case 'u':
                if (!append_unicode_escape(output, error)) return false;
                break;
            default: return set_error(error, "unsupported string escape");
            }
        }
        return set_error(error, "unterminated string");
    }

    bool read_unicode_unit(std::uint16_t* output, std::string* error)
    {
        std::uint16_t value = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ >= input_.size()) return set_error(error, "unterminated unicode escape");
            const char character = input_[position_++];
            unsigned int digit = 0;
            if (character >= '0' && character <= '9') digit = static_cast<unsigned int>(character - '0');
            else if (character >= 'a' && character <= 'f') digit = static_cast<unsigned int>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') digit = static_cast<unsigned int>(character - 'A' + 10);
            else return set_error(error, "invalid unicode escape");
            value = static_cast<std::uint16_t>((value << 4u) | digit);
        }
        *output = value;
        return true;
    }

    bool append_unicode_escape(std::string* output, std::string* error)
    {
        std::uint16_t first = 0;
        if (!read_unicode_unit(&first, error)) return false;
        std::uint32_t codepoint = first;
        if (first >= 0xd800u && first <= 0xdbffu) {
            if (position_ + 2u > input_.size() || input_[position_] != '\\' || input_[position_ + 1u] != 'u') {
                return set_error(error, "high surrogate must be followed by a low surrogate");
            }
            position_ += 2u;
            std::uint16_t second = 0;
            if (!read_unicode_unit(&second, error)) return false;
            if (second < 0xdc00u || second > 0xdfffu) return set_error(error, "invalid low surrogate");
            codepoint = 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
        } else if (first >= 0xdc00u && first <= 0xdfffu) {
            return set_error(error, "unpaired low surrogate");
        }
        if (codepoint <= 0x7fu) output->push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffu) {
            output->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
            output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu) {
            output->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
            output->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            output->push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
            output->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
            output->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
        return true;
    }
};

const RoleDefinition* find_role(const std::string& role)
{
    const auto found = std::find_if(kRequiredRoles.begin(), kRequiredRoles.end(),
        [&](const RoleDefinition& definition) { return role == definition.role; });
    return found == kRequiredRoles.end() ? nullptr : &*found;
}

bool validate_registry(DocumentationRegistry* registry, std::string* error_message)
{
    if (!registry || registry->documents.empty()) return fail(error_message, "documentation registry is empty");
    std::set<std::string> roles;
    std::set<std::string> sources;
    std::set<std::string> destinations;
    for (const CanonicalDocument& document : registry->documents) {
        const RoleDefinition* definition = find_role(document.role);
        if (!definition) return fail(error_message, "unknown documentation role: " + document.role);
        if (document.distribution != definition->distribution) {
            return fail(error_message, "invalid distribution for role " + document.role);
        }
        if (!roles.insert(document.role).second) return fail(error_message, "duplicate documentation role: " + document.role);
        if (!is_normalized_relative_path(document.source)
            || (document.source.filename() != "LICENSE" &&
                document.source.extension() != ".md" && document.source.extension() != ".txt")) {
            return fail(error_message, "invalid canonical document source: " + document.source.generic_string());
        }
        if (!sources.insert(document.source.generic_string()).second) {
            return fail(error_message, "duplicate canonical document source: " + document.source.generic_string());
        }
        if (document.distribution == "package") {
            if (!is_normalized_relative_path(document.destination)
                || (document.destination.filename() != "LICENSE" &&
                    document.destination.extension() != ".md" && document.destination.extension() != ".txt")) {
                return fail(error_message, "package document requires a valid destination: " + document.role);
            }
            if (!destinations.insert(document.destination.generic_string()).second) {
                return fail(error_message, "duplicate package document destination: " + document.destination.generic_string());
            }
        } else if (!document.destination.empty()) {
            return fail(error_message, "repository document must not define a package destination: " + document.role);
        }
    }
    for (const RoleDefinition& definition : kRequiredRoles) {
        if (roles.count(definition.role) == 0u) {
            return fail(error_message, "missing mandatory documentation role: " + std::string(definition.role));
        }
    }
    if (registry->documents.size() != kRequiredRoles.size()) {
        return fail(error_message, "documentation registry does not match the closed role set");
    }
    return true;
}

bool validate_links(
    const fs::path& root,
    const std::vector<CanonicalDocument>& documents,
    const bool staged,
    std::string* error_message)
{
    for (const CanonicalDocument& entry : documents) {
        const fs::path relative = staged ? entry.destination : entry.source;
        const fs::path document_path = root / relative;
        std::string text;
        if (!read_bytes(document_path, &text)) {
            return fail(error_message, "registered document is unreadable: " + relative.generic_string());
        }
        std::size_t cursor = 0;
        while ((cursor = text.find("](", cursor)) != std::string::npos) {
            const std::size_t target_start = cursor + 2u;
            const std::size_t target_end = text.find(')', target_start);
            if (target_end == std::string::npos) break;
            std::string target = text.substr(target_start, target_end - target_start);
            cursor = target_end + 1u;
            const std::size_t title = target.find(" \"");
            if (title != std::string::npos) target.resize(title);
            if (target.empty() || target.front() == '#' || target.rfind("https://", 0) == 0u
                || target.rfind("http://", 0) == 0u || target.rfind("mailto:", 0) == 0u) {
                continue;
            }
            if (target.front() == '<' && target.back() == '>') target = target.substr(1, target.size() - 2u);
            const std::size_t fragment = target.find('#');
            if (fragment != std::string::npos) target.resize(fragment);
            const fs::path resolved = (document_path.parent_path() / fs::path(target)).lexically_normal();
            if (!fs::exists(resolved)) {
                return fail(error_message, "broken relative Markdown link in " + relative.generic_string() + ": " + target);
            }
        }
    }
    return true;
}

bool validate_release_document_parity(
    const fs::path& source_root, const ReleaseAuthority& release, std::string* error_message)
{
    std::string catalog;
    if (!read_bytes(source_root / "src/game/rva_catalog.json", &catalog)) {
        return fail(error_message, "RVA catalog is unreadable for release parity");
    }
    const std::array<fs::path, 2> release_documents{
        "README.md", "docs/BuildAndRelease.md"};
    for (const fs::path& relative : release_documents) {
        std::string document;
        if (!read_bytes(source_root / relative, &document)) {
            return fail(error_message, "release-facing document is unreadable: " + relative.generic_string());
        }
        std::string version;
        if (!contains_once(document,
                std::regex(R"(<!--\s*current-release-version:\s*([^\s]+)\s*-->)"),
                &version, "current-release-version marker", error_message)) return false;
        if (version != release.version) {
            return fail(error_message, "current-release-version marker contradicts release.json: " + relative.generic_string());
        }
    }
    std::string changelog;
    if (!read_bytes(source_root / "CHANGELOG.md", &changelog)) return fail(error_message, "CHANGELOG.md is unreadable");
    std::smatch top_entry;
    if (!std::regex_search(changelog, top_entry,
            std::regex(R"((?:^|\n)## ([0-9]+\.[0-9]+\.[0-9]+)(?: |\r?$))"))
        || top_entry[1].str() != release.version) {
        return fail(error_message, "CHANGELOG.md top release entry does not match release.json");
    }
    // Only build identities carry the `ff7rebirth-` prefix under an `"id"` key. The per-address
    // `"builds"` maps repeat the same identities as object keys, so anchoring on `"id"` collects
    // exactly the entries declared in the catalog's `builds` array.
    const std::vector<std::string> declared = regex_captures(catalog,
        std::regex(R"metadata("id"\s*:\s*"(ff7rebirth-[a-z0-9-]+)")metadata"));
    const std::set<std::string> cataloged_builds(declared.begin(), declared.end());
    if (cataloged_builds.empty()) {
        return fail(error_message, "rva_catalog.json declares no build identity");
    }
    if (cataloged_builds.size() != declared.size()) {
        return fail(error_message, "rva_catalog.json declares a duplicate build identity");
    }
    std::set<std::string> released_builds;
    for (const ReleaseTarget& target : release.targets) {
        released_builds.insert(target.supported_executable_catalog_id);
    }
    return cataloged_builds == released_builds
        || fail(error_message,
            "release.json targets do not cover exactly the rva_catalog.json build identities");
}

bool validate_ini_documentation(const fs::path& source_root, std::string* error_message)
{
    std::string ini;
    std::string guide;
    if (!read_bytes(source_root / "FF7RPianoSongs.example.ini", &ini)
        || !read_bytes(source_root / "README.md", &guide)) {
        return fail(error_message, "INI template or README.md is unreadable");
    }
    std::string section;
    std::istringstream lines(ini);
    const std::regex section_line(R"(^\s*\[([^\]]+)\]\s*$)");
    const std::regex setting_line(R"(^\s*([A-Za-z0-9_]+)\s*=)");
    std::size_t settings = 0;
    for (std::string line; std::getline(lines, line);) {
        std::smatch match;
        if (std::regex_match(line, match, section_line)) section = match[1].str();
        else if (std::regex_search(line, match, setting_line)) {
            if (section.empty()) return fail(error_message, "INI setting appears before a section");
            const std::string qualified = '`' + section + '.' + match[1].str() + '`';
            if (guide.find(qualified) == std::string::npos) {
                return fail(error_message, "README.md omits INI setting " + qualified);
            }
            ++settings;
        }
    }
    return settings != 0u || fail(error_message, "INI template contains no settings");
}

bool verify_song_format_examples(const std::string& text, std::string* error_message)
{
    std::size_t cursor = 0;
    std::size_t example_count = 0;
    while ((cursor = text.find("```json", cursor)) != std::string::npos) {
        const std::size_t begin = text.find('\n', cursor);
        const std::size_t end = begin == std::string::npos ? std::string::npos : text.find("```", begin);
        if (end == std::string::npos) return fail(error_message, "SongFormat has an unterminated JSON example");
        ++example_count;
        ff7rp::pipeline::ParsedSongSource source;
        const auto json = text.substr(begin, end - begin);
        const auto status = ff7rp::pipeline::parse_song_json_string(json, &source);
        if (!ff7rp::pipeline::test_target_native_assets().has_verified_authored_chord_voicing()
            && json.find("\"chord_voicings\"") != std::string::npos) {
            // The shared guide cannot qualify a capability absent from this build.
            if (status.ok()) return fail(error_message, "SongFormat voicing example accepted an unavailable capability");
            cursor = end + 3u;
            continue;
        }
        if (!status.ok()) return fail(error_message,
            "SongFormat example " + std::to_string(example_count) + " does not parse: " + status.message);
        const auto compile = [&](const ff7rp::pipeline::SongConfig& config) {
            ff7rp::pipeline::CompiledChart chart;
            const auto compiled = ff7rp::pipeline::compile_chart(config, &chart);
            return compiled.ok() || fail(error_message,
                "SongFormat example " + std::to_string(example_count) + " does not compile: " + compiled.message);
        };
        if (source.authored_profiles.empty()) {
            // MIDI examples need source files at load time; explicit examples compile here.
            if (source.config.notes_provided && !compile(source.config)) return false;
        } else {
            for (const auto& profile : source.authored_profiles) {
                auto config = source.config;
                config.difficulty = profile.difficulty;
                config.notes = profile.notes;
                if (!compile(config)) return false;
            }
        }
        cursor = end + 3u;
    }
    return example_count != 0 || fail(error_message, "SongFormat has no JSON examples");
}

bool verify_release_parser_rejections(std::string* error_message)
{
    // Synthetic JSON keeps parser coverage independent of release versions and
    // formatting in checked-in authority files.
    std::string registry_json = R"({"schema":"ff7rpianosongs.package-docs.v2","documents":[)";
    for (const auto& definition : kRequiredRoles) {
        if (registry_json.back() != '[') registry_json += ',';
        const std::string role = definition.role;
        registry_json += "{\"source\":\"" + role + ".md\",\"role\":\"" + role +
            "\",\"distribution\":\"" + definition.distribution + '"';
        if (std::string_view(definition.distribution) == "package") {
            registry_json += ",\"destination\":\"" + role + ".md\"";
        }
        registry_json += '}';
    }
    registry_json += "]}";
    DocumentationRegistry registry;
    if (!parse_documentation_registry(registry_json, &registry, error_message)) return false;
    for (const auto& [from, to] : std::array<std::pair<std::string, std::string>, 6>{{
             {"\"documents\":", "\"unknown\":true,\"documents\":"},
             {"\"source\":\"entrypoint.md\"", "\"source\":\"../entrypoint.md\""},
             {"\"role\":\"docs-index\"", "\"role\":\"entrypoint\""},
             {"\"role\":\"entrypoint\"", "\"role\":\"entrypoint\",\"role\":\"entrypoint\""},
             {"\"distribution\":\"package\"", "\"distribution\":\"repository\""},
             {"\"role\":\"entrypoint\",", ""},
         }}) {
        std::string invalid = registry_json;
        invalid.replace(invalid.find(from), from.size(), to);
        if (parse_documentation_registry(invalid, &registry, nullptr)) {
            return fail(error_message, "documentation registry parser accepted an invalid fixture");
        }
    }
    const auto release_json = [](const std::string& version, const bool include_game_build = true) {
        return R"({"schema":"ff7rpianosongs.release.v2","product":"FF7RPianoSongs","version":")" + version +
            R"(","platform":"win64","license":"MIT","targets":[{"game_build":"1.005","supported_executable_catalog_id":"ff7rebirth-test-win64-12345678","archive_basename":"FF7RPianoSongs-)" +
            version + "-win64" + (include_game_build ? "-ff7r1.005" : "") + "\"}]}";
    };
    ReleaseAuthority release;
    if (!parse_release_authority(release_json("2.3.4"), &release, error_message)
        || release.version != "2.3.4") return fail(error_message, "release parser rejected a valid version");
    for (const auto* version : {"39", "v39", "01.0.0", "0.1", "0.1.0-beta"}) {
        if (parse_release_authority(release_json(version), &release, nullptr)) {
            return fail(error_message, "release parser accepted a malformed version");
        }
    }
    return !parse_release_authority(release_json("2.3.4", false), &release, nullptr)
        || fail(error_message, "release parser accepted an archive without its game build");
}

} // namespace

bool verify_song_format_contract_text(
    const std::string_view text, std::string* error_message)
{
    std::vector<std::set<std::string>> tables(1);
    std::istringstream lines{std::string(text)};
    const std::regex field_row(R"(^\s*\|\s*`([^`]+)`\s*\|)");
    for (std::string line; std::getline(lines, line);) {
        std::smatch match;
        if (std::regex_search(line, match, field_row)) {
            tables.back().insert(match[1].str());
        } else if (line.find('|') == std::string::npos && !tables.back().empty()) {
            tables.emplace_back();
        }
    }
    const auto require_inventory_rows = [&](const auto& fields, const char* object_name) {
        return std::any_of(tables.begin(), tables.end(), [&](const auto& table) {
            return std::all_of(fields.begin(), fields.end(), [&](const auto field) {
                return table.contains(std::string(field));
            });
        }) || fail(error_message, "SongFormat lacks a complete " + std::string(object_name) + " field table");
    };
    if (!require_inventory_rows(ff7rp::pipeline::kSongJsonRootFields, "root")
        || !require_inventory_rows(ff7rp::pipeline::kSongJsonNoteFields, "note")
        || !require_inventory_rows(ff7rp::pipeline::kSongJsonProfileFields, "profile")
        || !require_inventory_rows(ff7rp::pipeline::kSongJsonMetronomeFields, "metronome")
        || !require_inventory_rows(ff7rp::pipeline::kSongJsonGainPointFields, "gain-envelope point")) return false;
    constexpr std::array<std::string_view, 2> source_names{{"song.mid", "song.midi"}};
    if (!require_documented_tokens(text, source_names, "MIDI source name", error_message)) return false;
    for (const auto& entry : ff7rp::pipeline::kSupportedNamedNoteValues) {
        if (!require_inventory_rows(std::array{entry.name}, "note-value")) {
            return fail(error_message, "SongFormat note-value table is missing or incorrect for " +
                std::string(entry.name));
        }
    }
    for (const auto& chord : ff7rp::pipeline::kVerifiedNativeChordConstituents) {
        std::string row = "| `" + std::string(chord.chord_id) + "` | ";
        for (std::size_t index = 0; index < chord.sound_count; ++index) {
            if (index != 0u) row += ", ";
            row += "`" + std::string(chord.sound_names[index]) + "`";
        }
        row += " |";
        if (text.find(row) == std::string_view::npos) {
            return fail(error_message, "SongFormat verified chord table is missing or incorrect for " +
                std::string(chord.chord_id));
        }
    }
    return true;
}

bool parse_release_authority(
    const std::string& json, ReleaseAuthority* release, std::string* error_message)
{
    if (!release) return fail(error_message, "release authority output is null");
    std::map<std::string, std::string> fields;
    std::vector<ReleaseTarget> targets;
    ReleaseJsonParser parser(json);
    if (!parser.parse(&fields, &targets, error_message)) return false;
    const std::set<std::string> expected{"schema", "product", "version", "platform", "license"};
    std::set<std::string> actual;
    for (const auto& [key, value] : fields) actual.insert(key);
    if (actual != expected) return fail(error_message, "release.json does not match the closed field set");
    if (fields["schema"] != "ff7rpianosongs.release.v2"
        || fields["product"] != "FF7RPianoSongs" || fields["platform"] != "win64"
        || fields["license"] != "MIT") {
        return fail(error_message, "release schema/product/platform/license is unsupported");
    }
    const std::regex semver(R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))");
    if (!std::regex_match(fields["version"], semver)) {
        return fail(error_message, "release version is not canonical three-part semantic versioning");
    }
    // Game versions follow the catalog schema, independently of product semver.
    const std::regex game_version(R"([0-9]+\.[0-9]+)");
    const std::regex catalog_identity(R"(ff7rebirth-[a-z0-9-]+)");
    std::set<std::string> game_builds;
    std::set<std::string> catalog_ids;
    for (const ReleaseTarget& target : targets) {
        if (!std::regex_match(target.game_build, game_version)) {
            return fail(error_message, "release target game build is not a two-part game version");
        }
        if (!std::regex_match(target.supported_executable_catalog_id, catalog_identity)) {
            return fail(error_message, "release executable catalog ID is malformed");
        }
        const std::string expected_basename = fields["product"] + '-' + fields["version"] + '-'
            + fields["platform"] + "-ff7r" + target.game_build;
        if (target.archive_basename != expected_basename) {
            return fail(error_message,
                "release archive basename is not derived from product/version/platform/game build");
        }
        if (!game_builds.insert(target.game_build).second
            || !catalog_ids.insert(target.supported_executable_catalog_id).second) {
            return fail(error_message, "release targets do not declare distinct builds");
        }
    }
    release->product = fields["product"];
    release->version = fields["version"];
    release->platform = fields["platform"];
    release->license = fields["license"];
    release->targets = std::move(targets);
    return true;
}

bool load_release_authority(
    const fs::path& source_root, ReleaseAuthority* release, std::string* error_message)
{
    std::string json;
    if (!read_bytes(source_root / "release.json", &json)) {
        return fail(error_message, "release.json is unreadable");
    }
    return parse_release_authority(json, release, error_message);
}

const ReleaseTarget* find_release_target(
    const ReleaseAuthority& release, const std::string& catalog_id)
{
    const auto found = std::find_if(release.targets.begin(), release.targets.end(),
        [&](const ReleaseTarget& target) {
            return target.supported_executable_catalog_id == catalog_id;
        });
    return found == release.targets.end() ? nullptr : &*found;
}

bool parse_documentation_registry(
    const std::string& json, DocumentationRegistry* registry, std::string* error_message)
{
    if (!registry) return fail(error_message, "documentation registry output is null");
    std::string schema;
    DocumentationRegistry parsed;
    RegistryJsonParser parser(json);
    if (!parser.parse(&schema, &parsed.documents, error_message)) return false;
    if (schema != kRegistrySchema) return fail(error_message, "unsupported documentation registry schema: " + schema);
    if (!validate_registry(&parsed, error_message)) return false;
    *registry = std::move(parsed);
    return true;
}

bool load_documentation_registry(
    const fs::path& source_root, DocumentationRegistry* registry, std::string* error_message)
{
    std::string json;
    if (!read_bytes(source_root / "package-docs.json", &json)) {
        return fail(error_message, "package-docs.json is unreadable");
    }
    return parse_documentation_registry(json, registry, error_message);
}

std::vector<CanonicalDocument> package_documents(const DocumentationRegistry& registry)
{
    std::vector<CanonicalDocument> documents;
    std::copy_if(registry.documents.begin(), registry.documents.end(), std::back_inserter(documents),
        [](const CanonicalDocument& document) { return document.distribution == "package"; });
    return documents;
}

bool load_release_metadata(
    const fs::path& source_root, ReleaseMetadata* metadata, std::string* error_message)
{
    if (!metadata) return fail(error_message, "release metadata output is null");
    // The audit describes the artifact this translation unit was compiled for, so hook inventory
    // and release target are both selected by that build identity rather than by the catalog
    // default. Catalog-wide invariants across every build belong to the catalog generator check.
    const std::string build_id{FF7RP_TARGET_BUILD_ID};

    ReleaseMetadata parsed;
    if (!load_release_authority(source_root, &parsed.release, error_message)) return false;
    const ReleaseTarget* target = find_release_target(parsed.release, build_id);
    if (!target) {
        return fail(error_message, "release.json declares no target for build identity " + build_id);
    }
    parsed.target = *target;
    parsed.pipeline_cache_version = ff7rp::pipeline::kPipelineCacheVersion;
    parsed.runtime_cache_magic.assign(ff7rp::pipeline::kRuntimeCacheMagic,
        sizeof(ff7rp::pipeline::kRuntimeCacheMagic));
    parsed.runtime_cache_format = ff7rp::pipeline::kRuntimeCacheFormat;

    parsed.ctest_registrations.assign(std::begin(kConfiguredCTestNames), std::end(kConfiguredCTestNames));
    namespace rva = ff7r::piano::game::rva;
    const ff7r::piano::game::HookSpec hooks[] = {
#include "game/generated/hook_specs.generated.inc"
    };
    for (const auto& hook : hooks) {
        parsed.hook_specs.emplace_back(hook.name);
        if (hook.required_for_release_startup) ++parsed.required_hook_specs;
    }
    *metadata = std::move(parsed);
    return true;
}

bool verify_documentation_parity_at(const fs::path& source_root, std::string* error_message)
{
    if (!verify_release_parser_rejections(error_message)) return false;
    ReleaseAuthority release;
    if (!load_release_authority(source_root, &release, error_message)) return false;
    DocumentationRegistry registry;
    if (!load_documentation_registry(source_root, &registry, error_message)) return false;
    const auto song_format = std::find_if(registry.documents.begin(), registry.documents.end(),
        [](const CanonicalDocument& document) { return document.role == "song-format"; });
    if (song_format == registry.documents.end() || song_format->distribution != "package"
        || song_format->source != fs::path("docs/SongFormat.md")
        || song_format->destination != fs::path("docs/SongFormat.md")) {
        return fail(error_message,
            "song-format package role must map docs/SongFormat.md to docs/SongFormat.md");
    }
    for (const CanonicalDocument& document : registry.documents) {
        if (!fs::is_regular_file(source_root / document.source)) {
            return fail(error_message, "missing registered canonical document: " + document.source.generic_string());
        }
    }
    std::string song_format_text;
    if (!read_bytes(source_root / song_format->source, &song_format_text)
        || !verify_song_format_contract_text(song_format_text, error_message)
        || !verify_song_format_examples(song_format_text, error_message)) return false;
    if (!validate_ini_documentation(source_root, error_message)) return false;
    if (!validate_release_document_parity(source_root, release, error_message)) return false;
    return validate_links(source_root, registry.documents, false, error_message);
}

bool verify_staged_documentation_parity(
    const fs::path& source_root, const fs::path& package_root, std::string* error_message)
{
    if (!verify_documentation_parity_at(source_root, error_message)) return false;
    DocumentationRegistry registry;
    if (!load_documentation_registry(source_root, &registry, error_message)) return false;
    const std::vector<CanonicalDocument> packaged = package_documents(registry);
    std::set<std::string> expected_text_files;
    for (const CanonicalDocument& document : packaged) {
        std::string canonical;
        std::string staged;
        if (!read_bytes(source_root / document.source, &canonical)
            || !read_bytes(package_root / document.destination, &staged)) {
            return fail(error_message, "staged package document is missing: " + document.destination.generic_string());
        }
        if (canonical != staged) {
            return fail(error_message, "staged package document differs byte-for-byte: " +
                document.destination.generic_string());
        }
        expected_text_files.insert(document.destination.generic_string());
    }

    for (const CanonicalDocument& document : registry.documents) {
        if (document.distribution == "repository" && fs::exists(package_root / document.source)) {
            return fail(error_message, "repository-only registered document is present in package: " +
                document.source.generic_string());
        }
    }

    std::error_code error;
    for (fs::recursive_directory_iterator it(package_root, error), end; it != end && !error; it.increment(error)) {
        if (!it->is_regular_file()) continue;
        const std::string extension = it->path().extension().generic_string();
        const std::string relative = fs::relative(it->path(), package_root).generic_string();
        if (expected_text_files.erase(relative) != 0u) continue;
        if (extension == ".md" || extension == ".txt") {
            return fail(error_message, "unexpected documentation/evidence in staged package: " + relative);
        }
    }
    if (error || !expected_text_files.empty()) {
        return fail(error_message, "staged package documentation inventory is incomplete");
    }
    return validate_links(package_root, packaged, true, error_message);
}

} // namespace ff7rp::tests
