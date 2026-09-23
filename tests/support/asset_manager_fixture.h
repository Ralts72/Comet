#pragma once

#include "scripting/script.h"
#include "audio/audio.h"
#include "common/file_io.h"
#include "asset/asset_manager.h"

#include "asset/artifact/mesh_artifact.h"
#include "asset/import/environment_importer.h"
#include "render/resource/environment.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/task_scheduler.h"
#include "support/blocked_worker.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"
#include "support/hdr_image.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Comet::Tests {
    inline std::vector<AssetHandle> completed_handles(
        Result<std::vector<AssetHandle>, Error> result) {
        if(!result) {
            ADD_FAILURE() << result.error().message;
            return {};
        }
        return std::move(result).value();
    }

    template<typename T> std::shared_ptr<T> loaded_asset(Result<std::shared_ptr<T>, Error> result) {
        if(!result) {
            ADD_FAILURE() << result.error().message;
            return {};
        }
        return std::move(result).value();
    }

    namespace {
        class TemporaryProject final {
        public:
            TemporaryProject() { std::filesystem::create_directories(paths().assets()); }

            [[nodiscard]] ProjectPaths paths() const { return ProjectPaths(m_directory.path()); }

            std::filesystem::path add_material(
                const AssetHandle handle, const std::string& template_name) const {
                const std::filesystem::path path = paths().assets() / "materials/test.mat";
                std::filesystem::create_directories(path.parent_path());
                EXPECT_TRUE(MaterialSerializer{}.save(
                    {.template_name = template_name, .texture_properties = {}}, path));
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Material}, metadata_path(path)));
                return path;
            }

            std::filesystem::path add_texture(const AssetHandle handle) const {
                const std::filesystem::path path = paths().assets() / "textures/test.png";
                std::filesystem::create_directories(path.parent_path());
                std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                               / "assets/textures/awesomeface.png",
                    path, std::filesystem::copy_options::overwrite_existing);
                EXPECT_TRUE(
                    MetadataSerializer{}.save({.handle = handle,
                                                  .type = AssetType::Texture,
                                                  .import_settings = TextureImportSettings{}},
                        metadata_path(path)));
                return path;
            }

            void add_textured_material(
                const AssetHandle handle, const AssetHandle texture_handle) const {
                const auto path =
                    paths().assets() / "materials" / (std::to_string(handle.value()) + ".mat");
                std::filesystem::create_directories(path.parent_path());
                EXPECT_TRUE(MaterialSerializer{}.save(
                    {.template_name = "test_template",
                        .texture_properties = {{"u_Texture0", texture_handle}}},
                    path));
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Material}, metadata_path(path)));
            }

            static void replace_texture(
                const std::filesystem::path& path, const bool restore_original = false) {
                std::filesystem::path source = "assets/textures/R-C.jpeg";
                if(restore_original) {
                    source = "assets/textures/awesomeface.png";
                }
                std::filesystem::copy_file(
                    std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / source, path,
                    std::filesystem::copy_options::overwrite_existing);
            }

            static void corrupt_texture(const std::filesystem::path& path) {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                output << "corrupted texture data";
            }

            std::filesystem::path add_mesh(const AssetHandle handle,
                const std::string_view primitive = R"({"attributes":{"POSITION":0},"indices":1})",
                const std::filesystem::path& relative_path = "meshes/test.gltf") const {
                const std::filesystem::path path = paths().assets() / relative_path;
                std::filesystem::create_directories(path.parent_path());
                write_mesh(path, primitive);
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Mesh}, metadata_path(path)));
                return path;
            }

            std::filesystem::path add_external_mesh(const AssetHandle handle) const {
                const std::filesystem::path path = paths().assets() / "meshes/external.gltf";
                const std::filesystem::path buffer = paths().assets() / "meshes/external.bin";
                std::filesystem::create_directories(path.parent_path());
                write_external_mesh_buffer(buffer, false);
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"external.bin"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]})";
                output.close();
                EXPECT_TRUE(MetadataSerializer{}.save(
                    {.handle = handle, .type = AssetType::Mesh}, metadata_path(path)));
                return buffer;
            }

            static void write_mesh(
                const std::filesystem::path& path, const std::string_view primitive) {
                std::ofstream output(path, std::ios::binary);
                output
                    << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[)"
                    << primitive << "]}]}";
            }

            static void write_external_mesh_buffer(
                const std::filesystem::path& path, const bool modified) {
                std::array<std::uint8_t, 42> data{};
                data[14] = modified ? 0x00 : 0x80;
                data[15] = modified ? 0x40 : 0x3f;
                data[30] = 0x80;
                data[31] = 0x3f;
                data[38] = 0x01;
                data[40] = 0x02;
                std::ofstream output(path, std::ios::binary);
                output.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
            }

        private:
            TemporaryDirectory m_directory;
        };

        bool contains_handle(const std::vector<AssetHandle>& handles, const AssetHandle expected) {
            return std::ranges::find(handles, expected) != handles.end();
        }

        bool has_issue_containing(const AssetScanReport& report, const std::string_view text) {
            return std::ranges::any_of(report.issues, [&](const AssetScanIssue& issue) {
                return issue.message.find(text) != std::string::npos;
            });
        }
    }

}
