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

    }

    Result<MeshImporter::SourceInfo> MeshImporter::inspect_source(
        const std::filesystem::path& source_path, const AssetImportLimits& limits) {
        using Inspection = Result<SourceInfo>;
        if(limits.mesh_working_bytes <= GLTF_IMPORT_OVERHEAD_BYTES)
            return Inspection::failure("Mesh working budget is too small");
        std::error_code error;
        const auto source_size = std::filesystem::file_size(source_path, error);
        if(error || source_size > limits.source_bytes)
            return Inspection::failure(
                "Cannot read mesh input or input exceeds the configured source budget: "
                + source_path.string());
        if(source_size > (limits.mesh_working_bytes - GLTF_IMPORT_OVERHEAD_BYTES) / 3)
            return Inspection::failure("Mesh exceeds its configured CPU working-set budget");
        auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
        if(!source)
            return Inspection::failure(
                import_error(source_path, fastgltf::getErrorMessage(source.error())));
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
            fastgltf::Options::DecomposeNodeMatrices, fastgltf::Category::All);
        if(!parsed)
            return Inspection::failure(
                import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
        auto paths = source_paths_for(source_path, parsed.get());
        auto source_bytes = sum_source_bytes(paths, limits.source_bytes);
        if(!source_bytes)
            return Inspection::failure(source_bytes.error());
        auto selection = standalone_mesh_selection(parsed.get());
        if(!selection)
            return Inspection::failure(selection.error());
        auto estimate =
            estimate_gltf_mesh(parsed.get(), selection.value(), source_bytes.value(), limits);
        if(!estimate)
            return Inspection::failure(estimate.error());
        return Inspection::success({estimate.value(), {std::next(paths.begin()), paths.end()}});
    }

    Result<std::size_t> MeshImporter::working_bytes(
        const std::filesystem::path& source_path, const AssetImportLimits& limits) {
        auto inspection = inspect_source(source_path, limits);
        if(!inspection)
            return Result<std::size_t>::failure(inspection.error());
        return Result<std::size_t>::success(inspection.value().working_bytes);
    }

    Result<MeshData> MeshImporter::import(const std::filesystem::path& source_path,
        const std::size_t memory_budget, const AssetImportLimits& limits) const {
        auto imported = import_with_dependencies(source_path, memory_budget, limits);
        if(!imported)
            return Result<MeshData>::failure(imported.error());
        return Result<MeshData>::success(std::move(imported).value().data);
    }

    Result<MeshImportData> MeshImporter::import_with_dependencies(
        const std::filesystem::path& source_path, const std::size_t memory_budget,
        const AssetImportLimits& limits) const {
        using Imported = Result<MeshImportData>;
        auto inspection = inspect_source(source_path, limits);
        if(!inspection)
            return Imported::failure(inspection.error());
        if(inspection.value().working_bytes > memory_budget)
            return Imported::failure("Mesh exceeds its reserved CPU memory budget");
        auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
        if(!source) {
            return Imported::failure(
                import_error(source_path, fastgltf::getErrorMessage(source.error())));
        }

        constexpr auto options =
            fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(
            source.get(), source_path.parent_path(), options, fastgltf::Category::All);
        if(!parsed) {
            return Imported::failure(
                import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
        }

        fastgltf::Asset asset = std::move(parsed.get());
        auto source_paths = inspection.value().dependencies;
        source_paths.push_back(source_path);
        auto source_bytes = sum_source_bytes(source_paths, limits.source_bytes);
        if(!source_bytes)
            return Imported::failure(source_bytes.error());
        auto selection = standalone_mesh_selection(asset);
        if(!selection)
            return Imported::failure(import_error(source_path, selection.error()));
        auto current_estimate =
            estimate_gltf_mesh(asset, selection.value(), source_bytes.value(), limits);
        if(!current_estimate || current_estimate.value() > memory_budget)
            return Imported::failure("Mesh exceeds its reserved CPU memory budget");
        const fastgltf::Error validation_error = fastgltf::validate(asset);
        if(validation_error != fastgltf::Error::None) {
            return Imported::failure(
                import_error(source_path, fastgltf::getErrorMessage(validation_error)));
        }
        auto data = import_gltf_mesh(asset, selection.value(), source_path);
        if(!data)
            return Imported::failure(data.error());
        return Imported::success({.data = std::move(data).value(),
            .source_dependencies = std::move(inspection).value().dependencies});
    }
}
