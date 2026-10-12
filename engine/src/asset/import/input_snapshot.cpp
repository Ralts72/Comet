#include "asset/import/input_snapshot.h"
#include "common/binary.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace Comet {
    namespace {
        [[nodiscard]] bool is_safe_relative_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute()) {
                return false;
            }
            for(const std::filesystem::path& component : path) {
                if(component == "..") {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Result<std::filesystem::path> canonical_path(
            const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
            if(error) {
                return Result<std::filesystem::path>::failure(
                    "Failed to resolve import input path '" + path.string()
                    + "': " + error.message());
            }
            return Result<std::filesystem::path>::success(std::move(result));
        }

        [[nodiscard]] Result<std::filesystem::path> relative_to_root(
            const std::filesystem::path& canonical_root, const std::filesystem::path& path) {
            const auto canonical = canonical_path(path);
            if(!canonical)
                return Result<std::filesystem::path>::failure(canonical.error());
            const std::filesystem::path relative =
                canonical.value().lexically_relative(canonical_root).lexically_normal();
            if(!is_safe_relative_path(relative)) {
                return Result<std::filesystem::path>::failure(
                    "Import input is outside the asset root: " + path.string());
            }
            return Result<std::filesystem::path>::success(relative);
        }

        [[nodiscard]] std::optional<ImportInputFingerprint> fingerprint_file(
            const std::filesystem::path& path, std::filesystem::path relative_path) {
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                return std::nullopt;
            }

            ImportInputFingerprint fingerprint{
                .relative_path = std::move(relative_path), .hash = Binary::HASH_SEED};
            std::array<char, 64 * 1024> buffer{};
            while(input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if(count <= 0) {
                    continue;
                }
                if(static_cast<std::uint64_t>(count)
                    > std::numeric_limits<std::uint64_t>::max() - fingerprint.size) {
                    return std::nullopt;
                }
                fingerprint.size += static_cast<std::uint64_t>(count);
                fingerprint.hash = Binary::hash_bytes(
                    std::as_bytes(std::span(buffer).first(static_cast<std::size_t>(count))),
                    fingerprint.hash);
            }
            if(!input.eof()) {
                return std::nullopt;
            }
            return fingerprint;
        }
    }

    Result<ImportInputSnapshot> capture_import_inputs(const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        const std::span<const std::filesystem::path> source_dependencies) {
        const auto root = canonical_path(asset_root);
        if(!root)
            return Result<ImportInputSnapshot>::failure(root.error());
        const auto source = relative_to_root(root.value(), source_path);
        if(!source)
            return Result<ImportInputSnapshot>::failure(source.error());

        std::map<std::string, std::filesystem::path> dependencies;
        for(const auto& dependency : source_dependencies) {
            auto relative = relative_to_root(root.value(), dependency);
            if(!relative)
                return Result<ImportInputSnapshot>::failure(relative.error());
            if(relative.value() != source.value())
                dependencies.emplace(relative.value().generic_string(), relative.value());
        }

        ImportInputSnapshot snapshot;
        snapshot.files.reserve(dependencies.size() + 1);
        const auto add_file = [&](const std::filesystem::path& relative) {
            auto fingerprint = fingerprint_file(root.value() / relative, relative);
            if(!fingerprint)
                return Result<void>::failure("Failed to fingerprint import input '"
                                             + (root.value() / relative).string() + "'");
            snapshot.files.push_back(std::move(*fingerprint));
            return Result<void>::success();
        };
        if(auto added = add_file(source.value()); !added)
            return Result<ImportInputSnapshot>::failure(added.error());
        for(const auto& [path, relative] : dependencies) {
            if(auto added = add_file(relative); !added)
                return Result<ImportInputSnapshot>::failure(added.error());
        }
        return Result<ImportInputSnapshot>::success(std::move(snapshot));
    }

    bool import_inputs_are_current(
        const std::filesystem::path& asset_root, const ImportInputSnapshot& snapshot) {
        if(!snapshot.valid()) {
            return false;
        }

        const auto root = canonical_path(asset_root);
        if(!root)
            return false;
        const auto& canonical_root = root.value();
        for(const auto& expected : snapshot.files) {
            const auto& relative = expected.relative_path;
            const std::filesystem::path absolute = canonical_root / relative;
            const auto resolved = relative_to_root(canonical_root, absolute);
            if(!resolved || resolved.value() != relative) {
                return false;
            }
            const auto current = fingerprint_file(absolute, relative);
            if(!current || *current != expected) {
                return false;
            }
        }
        return true;
    }
}
