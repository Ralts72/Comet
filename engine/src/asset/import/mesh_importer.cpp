#include "asset/import/mesh_importer.h"
#include "asset/default_assets.h"
#include "asset/data/mesh_data.h"
#include "asset/import/gltf_mesh.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    std::filesystem::path default_character_source() {
        return COMET_DEFAULT_CHARACTER_FILE;
    }

    namespace {

        std::string import_error(
            const std::filesystem::path& source_path, const std::string_view message) {
            return "Failed to import mesh '" + source_path.string() + "': " + std::string(message);
        }

        Result<std::size_t> sum_source_bytes(
            const std::vector<std::filesystem::path>& files, const std::size_t limit) {
            std::size_t total = 0;
            for(const auto& path : files) {
                std::error_code error;
                const auto size = std::filesystem::file_size(path, error);
                if(error || size > limit - total)
                    return Result<std::size_t>::failure(
                        "Cannot read mesh input or inputs exceed the configured source budget: "
                        + path.string());
                total += static_cast<std::size_t>(size);
            }
            return Result<std::size_t>::success(total);
        }

        std::vector<std::filesystem::path> source_paths_for(
            const std::filesystem::path& source_path, const fastgltf::Asset& asset) {
            std::vector<std::filesystem::path> files{source_path.lexically_normal()};
            for(const fastgltf::Buffer& buffer : asset.buffers) {
                const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data);
                if(!uri || !uri->uri.isLocalPath())
                    continue;
                const auto path =
                    (source_path.parent_path() / uri->uri.fspath()).lexically_normal();
                if(std::ranges::find(files, path) == files.end())
                    files.push_back(path);
            }
            return files;
        }

        struct MeshInspection {
            std::size_t working_bytes;
            std::vector<std::filesystem::path> source_paths;
        };

        Result<MeshInspection> inspect_mesh(
            const std::filesystem::path& source_path, const AssetImportLimits& limits) {
            if(limits.mesh_working_bytes <= GLTF_IMPORT_OVERHEAD_BYTES)
                return Result<MeshInspection>::failure("Mesh working budget is too small");
            std::error_code error;
            const auto source_size = std::filesystem::file_size(source_path, error);
            if(error || source_size > limits.source_bytes)
                return Result<MeshInspection>::failure(
                    "Cannot read mesh input or input exceeds the configured source budget: "
                    + source_path.string());
            if(source_size > (limits.mesh_working_bytes - GLTF_IMPORT_OVERHEAD_BYTES) / 3)
                return Result<MeshInspection>::failure(
                    "Mesh exceeds its configured CPU working-set budget");
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                return Result<MeshInspection>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::DecomposeNodeMatrices, fastgltf::Category::All);
            if(!parsed)
                return Result<MeshInspection>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
            auto paths = source_paths_for(source_path, parsed.get());
            auto source_bytes = sum_source_bytes(paths, limits.source_bytes);
            if(!source_bytes)
                return Result<MeshInspection>::failure(source_bytes.error());
            auto selection = standalone_mesh_selection(parsed.get());
            if(!selection)
                return Result<MeshInspection>::failure(selection.error());
            auto estimate =
                estimate_gltf_mesh(parsed.get(), selection.value(), source_bytes.value(), limits);
            if(!estimate)
                return Result<MeshInspection>::failure(estimate.error());
            return Result<MeshInspection>::success({estimate.value(), std::move(paths)});
        }

        [[nodiscard]] Result<std::vector<std::filesystem::path>> collect_source_dependencies(
            const std::filesystem::path& source_path) {
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                return Result<std::vector<std::filesystem::path>>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::None, fastgltf::Category::Buffers);
            if(!parsed)
                return Result<std::vector<std::filesystem::path>>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
            auto paths = source_paths_for(source_path, parsed.get());
            return Result<std::vector<std::filesystem::path>>::success(
                {std::next(paths.begin()), paths.end()});
        }

    }

    Result<std::size_t> MeshImporter::working_bytes(
        const std::filesystem::path& source_path, const AssetImportLimits& limits) {
        auto inspection = inspect_mesh(source_path, limits);
        if(!inspection)
            return Result<std::size_t>::failure(inspection.error());
        return Result<std::size_t>::success(inspection.value().working_bytes);
    }

    Result<MeshData> MeshImporter::import(const std::filesystem::path& source_path,
        const std::size_t memory_budget, const AssetImportLimits& limits) const {
        auto inspection = inspect_mesh(source_path, limits);
        if(!inspection)
            return Result<MeshData>::failure(inspection.error());
        if(inspection.value().working_bytes > memory_budget)
            return Result<MeshData>::failure("Mesh exceeds its reserved CPU memory budget");
        auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
        if(!source) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(source.error())));
        }

        constexpr auto options =
            fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(
            source.get(), source_path.parent_path(), options, fastgltf::Category::All);
        if(!parsed) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
        }

        fastgltf::Asset asset = std::move(parsed.get());
        auto source_bytes = sum_source_bytes(inspection.value().source_paths, limits.source_bytes);
        if(!source_bytes)
            return Result<MeshData>::failure(source_bytes.error());
        auto selection = standalone_mesh_selection(asset);
        if(!selection)
            return Result<MeshData>::failure(import_error(source_path, selection.error()));
        auto current_estimate =
            estimate_gltf_mesh(asset, selection.value(), source_bytes.value(), limits);
        if(!current_estimate || current_estimate.value() > memory_budget)
            return Result<MeshData>::failure("Mesh exceeds its reserved CPU memory budget");
        const fastgltf::Error validation_error = fastgltf::validate(asset);
        if(validation_error != fastgltf::Error::None) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(validation_error)));
        }
        return import_gltf_mesh(asset, selection.value(), source_path);
    }

    Result<MeshImportData> MeshImporter::import_with_dependencies(
        const std::filesystem::path& source_path, const std::size_t memory_budget,
        const AssetImportLimits& limits) const {
        auto data = import(source_path, memory_budget, limits);
        if(!data)
            return Result<MeshImportData>::failure(data.error());
        auto dependencies = collect_source_dependencies(source_path);
        if(!dependencies)
            return Result<MeshImportData>::failure(dependencies.error());
        return Result<MeshImportData>::success({.data = std::move(data).value(),
            .source_dependencies = std::move(dependencies).value()});
    }
}
