#include "asset/import/input_snapshot.h"

#include <algorithm>
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
        constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
        constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

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

        [[nodiscard]] AssetResult<std::filesystem::path> canonical_path(
            const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
            if(error) {
                return AssetResult<std::filesystem::path>::failure(
                    "Failed to resolve import input path '" + path.string()
                    + "': " + error.message());
            }
            return AssetResult<std::filesystem::path>::success(std::move(result));
        }

        [[nodiscard]] AssetResult<std::filesystem::path> relative_to_root(
            const std::filesystem::path& canonical_root,
            const std::filesystem::path& path) {
            const auto canonical = canonical_path(path);
            if(!canonical)
                return AssetResult<std::filesystem::path>::failure(canonical.error());
            const std::filesystem::path relative =
                canonical.value().lexically_relative(canonical_root).lexically_normal();
            if(!is_safe_relative_path(relative)) {
                return AssetResult<std::filesystem::path>::failure(
                    "Import input is outside the asset root: " + path.string());
            }
            return AssetResult<std::filesystem::path>::success(relative);
        }

        [[nodiscard]] std::optional<ImportInputFingerprint> fingerprint_file(
            const std::filesystem::path& path, std::filesystem::path relative_path) {
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                return std::nullopt;
            }

            ImportInputFingerprint fingerprint{
                .relative_path = std::move(relative_path), .hash = FNV_OFFSET_BASIS};
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
                for(std::streamsize index = 0; index < count; ++index) {
                    fingerprint.hash ^= static_cast<unsigned char>(buffer[index]);
                    fingerprint.hash *= FNV_PRIME;
                }
            }
            if(!input.eof()) {
                return std::nullopt;
            }
            return fingerprint;
        }
    }

    AssetResult<ImportInputSnapshot> capture_import_inputs(
        const std::filesystem::path& asset_root, const std::filesystem::path& source_path,
        const std::span<const std::filesystem::path> source_dependencies) {
        const auto root = canonical_path(asset_root);
        if(!root)
            return AssetResult<ImportInputSnapshot>::failure(root.error());
        const auto source = relative_to_root(root.value(), source_path);
        if(!source)
            return AssetResult<ImportInputSnapshot>::failure(source.error());

        std::map<std::string, std::filesystem::path> dependencies;
        for(const auto& dependency : source_dependencies) {
            auto relative = relative_to_root(root.value(), dependency);
            if(!relative)
                return AssetResult<ImportInputSnapshot>::failure(relative.error());
            if(relative.value() != source.value())
                dependencies.emplace(relative.value().generic_string(), relative.value());
        }

        ImportInputSnapshot snapshot;
        snapshot.files.reserve(dependencies.size() + 1);
        const auto add_file = [&](const std::filesystem::path& relative) {
            auto fingerprint = fingerprint_file(root.value() / relative, relative);
            if(!fingerprint)
                return AssetResult<void>::failure("Failed to fingerprint import input '"
                                                  + (root.value() / relative).string()
                                                  + "'");
            snapshot.files.push_back(std::move(*fingerprint));
            return AssetResult<void>::success();
        };
        if(auto added = add_file(source.value()); !added)
            return AssetResult<ImportInputSnapshot>::failure(added.error());
        for(const auto& [path, relative] : dependencies) {
            if(auto added = add_file(relative); !added)
                return AssetResult<ImportInputSnapshot>::failure(added.error());
        }
        return AssetResult<ImportInputSnapshot>::success(std::move(snapshot));
    }

    bool import_inputs_are_current(
        const std::filesystem::path& asset_root, const ImportInputSnapshot& snapshot) {
        if(snapshot.files.empty()) {
            return false;
        }

        const auto root = canonical_path(asset_root);
        if(!root)
            return false;
        const auto& canonical_root = root.value();
        const std::filesystem::path source_relative =
            snapshot.files.front().relative_path.lexically_normal();
        std::filesystem::path previous_dependency;
        for(std::size_t index = 0; index < snapshot.files.size(); ++index) {
            const ImportInputFingerprint& expected = snapshot.files[index];
            const std::filesystem::path relative =
                expected.relative_path.lexically_normal();
            if(!is_safe_relative_path(relative) || relative != expected.relative_path
                || (index > 0 && relative == source_relative)
                || (index > 1
                    && relative.generic_string()
                           <= previous_dependency.generic_string())) {
                return false;
            }

            const std::filesystem::path absolute = canonical_root / relative;
            const auto resolved = relative_to_root(canonical_root, absolute);
            if(!resolved || resolved.value() != relative) {
                return false;
            }
            const auto current = fingerprint_file(absolute, relative);
            if(!current || *current != expected) {
                return false;
            }
            if(index > 0) {
                previous_dependency = relative;
            }
        }
        return true;
    }
}
