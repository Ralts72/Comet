#include "support/demo_project.h"
#include "asset/import/model_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/import/import_service.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/database.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "core/task_scheduler.h"
#include "render/material/material.h"
#include "support/render_resource_factory.h"
#include "asset/serialization/metadata_serializer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/project_prepare.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/prefab.h"
#include "asset/default_assets.h"
#include "common/binary.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace Comet::Tests {
    namespace {
        class ModelInputs {
        public:
            TemporaryDirectory directory;

            std::filesystem::path write(std::string_view name, std::string_view text) const {
                const auto path = directory.path() / name;
                std::ofstream(path, std::ios::binary) << text;
                return path;
            }

            static Binary::Writer geometry() {
                Binary::Writer data;
                for(const auto position :
                    std::array{Math::Vec3(0, 0, 0), Math::Vec3(1, 0, 0), Math::Vec3(0, 1, 0)}) {
                    for(float value : {position.x, position.y, position.z, position.x, position.y})
                        data.write_float(value);
                    data.write_u32(0); // Four uint8 joint indices.
                    for(float value : {1.f, 0.f, 0.f, 0.f})
                        data.write_float(value);
                }
                for(uint32_t index : {0u, 1u, 2u})
                    data.write_u32(index);
                return data;
            }

            static std::string document(std::string_view nodes, std::string_view extra = {},
                bool skinned = false, bool embedded = false) {
                const std::string attributes =
                    skinned ? R"({"POSITION":0,"TEXCOORD_0":1,"JOINTS_0":2,"WEIGHTS_0":3})"
                            : R"({"POSITION":0,"TEXCOORD_0":1})";
                const auto primitive = R"({"attributes":)" + attributes + R"(,"indices":4)";
                return R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":132)"
                       + std::string(embedded ? "" : R"(,"uri":"geometry.bin")")
                       + R"(}],"bufferViews":[{"buffer":0,"byteLength":120,"byteStride":40},
                        {"buffer":0,"byteOffset":120,"byteLength":12}],"accessors":[
                        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                        {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC2"},
                        {"bufferView":0,"byteOffset":20,"componentType":5121,"count":3,"type":"VEC4"},
                        {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC4"},
                        {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],
                        "meshes":[{"name":"Two slots","primitives":[)"
                       + primitive + R"(,"material":0},)" + primitive
                       + R"(,"material":1}]},{"name":"No material","primitives":[)" + primitive
                       + R"(}]}],"nodes":)" + std::string(nodes) + std::string(extra) + "}";
            }

            std::filesystem::path model(
                std::string json, bool glb = false, std::span<const std::byte> image = {}) const {
                auto data = geometry();
                if(!image.empty()) {
                    data.write_bytes(image);
                    json.replace(json.find("132"), 3, std::to_string(data.data().size()));
                    json.insert(json.find("],\"accessors\""),
                        R"(,{"buffer":0,"byteOffset":132,"byteLength":)"
                            + std::to_string(image.size()) + "}");
                    while(data.data().size() % 4)
                        data.write_bytes(std::span<const std::byte>(image.data(), 1));
                }
                if(!glb) {
                    std::ofstream output(directory.path() / "geometry.bin", std::ios::binary);
                    output.write(
                        reinterpret_cast<const char*>(data.data().data()), data.data().size());
                    return write("model.gltf", json);
                }
                while(json.size() % 4)
                    json += ' ';
                Binary::Writer file;
                file.write_u32(0x46546c67);
                file.write_u32(2);
                file.write_u32(uint32_t(12 + 8 + json.size() + 8 + data.data().size()));
                file.write_u32(uint32_t(json.size()));
                file.write_u32(0x4e4f534a);
                file.write_bytes(std::as_bytes(std::span(json)));
                file.write_u32(uint32_t(data.data().size()));
                file.write_u32(0x004e4942);
                file.write_bytes(data.data());
                const auto& bytes = file.data();
                return write(
                    "model.glb", {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            }
        };

        constexpr std::string_view MATERIALS = R"(,"materials":[{"name":"A"},{"name":"B"}])";
    }

    TEST(ModelImporterTest,
        PreservesHierarchySceneRootsAndPrimitiveMaterialsWithoutBakingTransforms) {
        const ModelInputs inputs;
        for(bool glb : {false, true}) {
            const auto source = inputs.model(
                ModelInputs::document(
                    R"([{"name":"Group","children":[1,2],"translation":[10,0,0]},
                    {"name":"Child","mesh":0,"scale":[-2,1,1]},
                    {"name":"Other","mesh":1,"translation":[0,3,0]},
                    {"name":"Instance","mesh":0,"translation":[0,0,5]}])",
                    std::string(MATERIALS) + R"(,"scenes":[{"nodes":[0]},{"nodes":[3]}],"scene":1)",
                    false, glb),
                glb);
            const auto estimate = ModelImporter::working_bytes(source);
            ASSERT_TRUE(estimate) << estimate.error();
            const auto imported = ModelImporter{}.import(source, estimate.value());
            ASSERT_TRUE(imported) << imported.error();
            const auto& model = imported.value();
            ASSERT_EQ(model.nodes.size(), 4u);
            ASSERT_EQ(model.meshes.size(), 3u);
            EXPECT_EQ(model.nodes[1].parent, 0u);
            EXPECT_EQ(model.nodes[2].parent, 0u);
            EXPECT_EQ(model.nodes[3].parent, ModelImportData::NONE);
            EXPECT_EQ(model.nodes[0].transform.translation, Math::Vec3(10, 0, 0));
            EXPECT_EQ(model.nodes[1].transform.scale, Math::Vec3(-2, 1, 1));
            EXPECT_EQ(model.nodes[1].meshes, model.nodes[3].meshes);
            EXPECT_EQ(model.nodes[1].meshes.size(), 2u);
            EXPECT_EQ(model.meshes[0].material, 0u);
            EXPECT_EQ(model.meshes[1].material, 1u);
            EXPECT_EQ(model.meshes[2].material, ModelImportData::NONE);
            EXPECT_EQ(model.meshes[1].source_mesh, 0u);
            EXPECT_EQ(model.meshes[1].source_primitive, 1u);
            EXPECT_EQ(model.meshes[0].data.vertices[1].position, Math::Vec3(1, 0, 0));
            EXPECT_EQ(model.default_scene, 1u);
            ASSERT_EQ(model.scenes.size(), 2u);
            EXPECT_EQ(model.scenes[0].roots, std::vector<uint32_t>{0});
            EXPECT_EQ(model.scenes[1].roots, std::vector<uint32_t>{3});
            EXPECT_EQ(model.source_dependencies.size(), glb ? 0u : 1u);
            EXPECT_FALSE(ModelImporter{}.import(source, estimate.value() - 1));
        }
    }

    TEST(ModelImporterTest, ImportsPbrTextureRolesSamplersAndEncodedImages) {
        const ModelInputs inputs;
        const std::string png =
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aXioAAAAASUVORK5CYII=";
        const auto external = inputs.write("surface.png", "external-image-bytes");
        const auto source = inputs.model(ModelInputs::document(R"([{"mesh":0}])",
            R"(,"materials":[{"name":"Paint","pbrMetallicRoughness":{
                "baseColorFactor":[0.2,0.3,0.4,0.5],"metallicFactor":0.7,"roughnessFactor":0.6,
                "baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}},
                "normalTexture":{"index":1,"scale":0.8},"occlusionTexture":{"index":1,"strength":0.4},
                "emissiveTexture":{"index":0},"emissiveFactor":[0.1,0.2,0.3],
                "alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true}, {"alphaMode":"BLEND"}],
                "images":[{"uri":"surface.png"},{"uri":"data:image/png;base64,)"
                + png + R"("}],
                "textures":[{"source":0,"sampler":0},{"source":1}],
                "samplers":[{"magFilter":9728,"minFilter":9729,"wrapS":33071,"wrapT":33648}])"));
        const auto imported = ModelImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        const auto& model = imported.value();
        ASSERT_EQ(model.materials.size(), 2u);
        const auto& material = model.materials[0];
        EXPECT_EQ(material.data.vector_properties.at("base_color"), Math::Vec4(.2f, .3f, .4f, .5f));
        EXPECT_FLOAT_EQ(material.data.scalar_properties.at("metallic"), .7f);
        EXPECT_FLOAT_EQ(material.data.scalar_properties.at("normal_scale"), .8f);
        EXPECT_FLOAT_EQ(material.data.scalar_properties.at("occlusion_strength"), .4f);
        EXPECT_EQ(material.data.surface.alpha_mode, AlphaMode::Mask);
        EXPECT_FLOAT_EQ(material.data.surface.alpha_cutoff, .3f);
        EXPECT_TRUE(material.data.surface.double_sided);
        EXPECT_EQ(model.materials[1].data.surface.alpha_mode, AlphaMode::Blend);
        ASSERT_EQ(material.textures.size(), 5u);
        for(const auto& binding : material.textures) {
            const bool color =
                binding.property == "base_color_texture" || binding.property == "emissive_texture";
            EXPECT_EQ(binding.settings.color_space,
                color ? TextureColorSpace::Srgb : TextureColorSpace::Linear);
            EXPECT_EQ(binding.settings.normal_map, binding.property == "normal_texture");
            EXPECT_EQ(binding.settings.generate_mipmaps, !color);
            EXPECT_EQ(binding.texture, color ? 0u : 1u);
        }
        ASSERT_EQ(model.textures.size(), 2u);
        EXPECT_EQ(model.textures[0].sampler.mag_filter, Filter::Nearest);
        EXPECT_EQ(model.textures[0].sampler.min_filter, Filter::Linear);
        EXPECT_EQ(model.textures[0].sampler.address_mode_u, SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(model.textures[0].sampler.address_mode_v, SamplerAddressMode::MirrorRepeat);
        ASSERT_EQ(model.images.size(), 2u);
        const auto& encoded = model.images[0].encoded;
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()),
            "external-image-bytes");
        EXPECT_GT(model.images[1].encoded.size(), 8u);
        EXPECT_EQ(model.images[1].encoded[0], std::byte{0x89});
        const auto decoded =
            TextureImporter{}.import(std::span<const std::byte>(model.images[1].encoded));
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value().width, 1);
        EXPECT_EQ(decoded.value().height, 1);
        EXPECT_NE(std::ranges::find(model.source_dependencies, external),
            model.source_dependencies.end());
    }

    TEST(ModelImporterTest, SeparatesSkinBindingsForSharedGeometry) {
        const ModelInputs inputs;
        const auto source = inputs.model(ModelInputs::document(
            R"([{"mesh":0,"skin":0},{"mesh":0,"skin":1,"translation":[2,0,0]},{},{}])",
            std::string(MATERIALS) + R"(,"skins":[{"joints":[2]},{"joints":[3]}])", true));
        const auto imported = ModelImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        const auto& model = imported.value();
        ASSERT_EQ(model.meshes.size(), 4u);
        EXPECT_NE(model.nodes[0].meshes, model.nodes[1].meshes);
        for(size_t index = 0; index < model.meshes.size(); ++index) {
            const auto& mesh = model.meshes[index];
            ASSERT_TRUE(mesh.data.animation);
            EXPECT_EQ(mesh.skin_node, index / 2);
            EXPECT_EQ(mesh.data.animation->mesh_node, index / 2);
            ASSERT_EQ(mesh.data.animation->joints.size(), 1u);
            EXPECT_EQ(mesh.data.animation->joints[0].node, 2 + index / 2);
            EXPECT_EQ(mesh.data.skin.size(), mesh.data.vertices.size());
        }
    }

    TEST(ModelImporterTest, DecodesGlbImageBufferViewWithoutExternalImageFiles) {
        const ModelInputs inputs;
        const auto image = read_binary_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                                / "assets/textures/awesomeface.png",
            1024 * 1024);
        ASSERT_TRUE(image);
        const auto source = inputs.model(
            ModelInputs::document(R"([{"mesh":0}])",
                std::string(MATERIALS) + R"(,"images":[{"bufferView":2,"mimeType":"image/png"}])",
                false, true),
            true, image.value());
        const auto imported = ModelImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        EXPECT_TRUE(imported.value().source_dependencies.empty());
        ASSERT_EQ(imported.value().images.size(), 1u);
        EXPECT_EQ(imported.value().images[0].encoded, image.value());
        const auto texture = TextureImporter{}.import(
            std::span<const std::byte>(imported.value().images[0].encoded));
        ASSERT_TRUE(texture) << texture.error();
        EXPECT_EQ(texture.value().width, 512);
        EXPECT_EQ(texture.value().mip_levels, 10u);
    }

    TEST(ModelImporterTest, RejectsBrokenHierarchyAndMissingCompanions) {
        const ModelInputs inputs;
        for(auto nodes : {R"([{"children":[1]},{"children":[0]}])",
                R"([{"children":[2]},{"children":[2]},{}])"}) {
            const auto source = inputs.model(ModelInputs::document(nodes, MATERIALS));
            EXPECT_FALSE(ModelImporter{}.import(source));
        }
        const auto source = inputs.model(ModelInputs::document(R"([{"mesh":0}])", MATERIALS));
        const auto estimate = ModelImporter::working_bytes(source);
        ASSERT_TRUE(estimate) << estimate.error();
        AssetImportLimits limits;
        limits.mesh_working_bytes = estimate.value() - 1;
        EXPECT_FALSE(ModelImporter::working_bytes(source, limits));
        std::filesystem::remove(inputs.directory.path() / "geometry.bin");
        EXPECT_FALSE(ModelImporter{}.import(source));
    }

    TEST(ModelImporterTest, ReimportKeepsNamedSubresourceHandlesWhenSourceArraysMove) {
        const ModelInputs inputs;
        const auto source = inputs.model(ModelInputs::document(
            R"([{"name":"First","mesh":0},{"name":"Second","mesh":1}])", MATERIALS));
        const auto imported = ModelImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        const AssetMetadata metadata{.handle = AssetHandle{1234}, .type = AssetType::Model};
        const auto first = ModelImporter::bind(imported.value(), metadata);
        ASSERT_TRUE(first) << first.error();
        ASSERT_EQ(first.value().model.nodes[0].parts.size(), 2u);
        ASSERT_EQ(first.value().model.nodes[1].parts.size(), 1u);
        const auto default_material = first.value().model.nodes[1].parts[0].material;
        const auto found_default = std::ranges::find(
            first.value().materials, default_material, &ModelImportProduct::Material::handle);
        ASSERT_NE(found_default, first.value().materials.end());
        EXPECT_FLOAT_EQ(found_default->data.scalar_properties.at("metallic"), 1.f);

        auto changed = imported.value();
        std::swap(changed.materials[0], changed.materials[1]);
        changed.materials[1].data.scalar_properties["roughness"] = .25f;
        for(auto& mesh : changed.meshes) {
            mesh.source_mesh = 1 - mesh.source_mesh;
            if(mesh.material != ModelImportData::NONE)
                mesh.material = 1 - mesh.material;
        }
        const auto second = ModelImporter::bind(std::move(changed), first.value().metadata);
        ASSERT_TRUE(second) << second.error();
        EXPECT_EQ(second.value().metadata, first.value().metadata);
        for(size_t node = 0; node < first.value().model.nodes.size(); ++node) {
            EXPECT_EQ(second.value().model.nodes[node].key, first.value().model.nodes[node].key);
            for(size_t part = 0; part < first.value().model.nodes[node].parts.size(); ++part) {
                EXPECT_EQ(second.value().model.nodes[node].parts[part].mesh,
                    first.value().model.nodes[node].parts[part].mesh);
                EXPECT_EQ(second.value().model.nodes[node].parts[part].material,
                    first.value().model.nodes[node].parts[part].material);
            }
        }
        const auto paint = std::ranges::find(second.value().materials,
            first.value().model.nodes[0].parts[0].material, &ModelImportProduct::Material::handle);
        ASSERT_NE(paint, second.value().materials.end());
        EXPECT_FLOAT_EQ(paint->data.scalar_properties.at("roughness"), .25f);
    }

    TEST(ModelImporterTest, SeparatesTextureRolesAndSharesIdenticalBindings) {
        const ModelInputs inputs;
        const auto source = inputs.model(ModelInputs::document(R"([{"mesh":0}])", MATERIALS));
        auto imported = ModelImporter{}.import(source);
        ASSERT_TRUE(imported) << imported.error();
        auto& data = imported.value();
        data.images.push_back({"Surface.png", {std::byte{1}}});
        data.textures.push_back({.image = 0});
        data.materials[0].textures = {
            {"base_color_texture", 0, {.color_space = TextureColorSpace::Srgb}},
            {"emissive_texture", 0, {.color_space = TextureColorSpace::Srgb}},
            {"normal_texture", 0, {.color_space = TextureColorSpace::Linear, .normal_map = true}},
            {"metallic_roughness_texture", 0, {.color_space = TextureColorSpace::Linear}},
            {"occlusion_texture", 0, {.color_space = TextureColorSpace::Linear}}};
        data.materials[1].textures = {{"base_color_texture", 0, {}}};
        const auto bound = ModelImporter::bind(
            std::move(data), {.handle = AssetHandle{1234}, .type = AssetType::Model});
        ASSERT_TRUE(bound) << bound.error();
        ASSERT_EQ(bound.value().textures.size(), 3u);
        const auto& bindings = bound.value().materials[0].data.texture_properties;
        const auto color = bindings.at("base_color_texture");
        EXPECT_EQ(bindings.at("emissive_texture"), color);
        EXPECT_EQ(
            bound.value().materials[1].data.texture_properties.at("base_color_texture"), color);
        EXPECT_EQ(bindings.at("occlusion_texture"), bindings.at("metallic_roughness_texture"));
        EXPECT_NE(color, bindings.at("normal_texture"));
        EXPECT_NE(bindings.at("normal_texture"), bindings.at("occlusion_texture"));
    }

    TEST(ModelImporterTest, PublishesCompleteProductsAndKeepsHandlesAcrossSourceChanges) {
        const ModelInputs inputs;
        const auto original = inputs.model(
            ModelInputs::document(R"([{"name":"Model","mesh":0},{"name":"Other","mesh":1}])",
                R"(,"materials":[{"name":"Paint","pbrMetallicRoughness":{
                "baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}},
                "normalTexture":{"index":0}},{"name":"Glass","alphaMode":"BLEND"}],
                "textures":[{"source":0,"sampler":0}],"images":[{"uri":"surface.png"}],
                "samplers":[{"magFilter":9728,"minFilter":9984,"wrapS":33071,"wrapT":33648}])"));
        const ProjectPaths paths(inputs.directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto source = paths.assets() / "model.gltf";
        std::filesystem::copy_file(original, source);
        const auto geometry = paths.assets() / "geometry.bin";
        std::filesystem::copy_file(inputs.directory.path() / "geometry.bin", geometry);
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            paths.assets() / "surface.png");
        const AssetMetadata metadata{.handle = AssetHandle{1234}, .type = AssetType::Model};
        const MetadataSerializer serializer;
        ASSERT_TRUE(serializer.save(metadata, metadata_path(source)));
        AssetDatabase database(paths);
        ASSERT_TRUE(database.scan().succeeded());
        const auto record = *database.find(metadata.handle);
        const ImportService service(paths);
        const auto prepared = service.prepare_model(record, AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(prepared) << prepared.error();
        const auto index_path = service.model_artifact_path(metadata.handle);
        const auto loaded = ModelArtifact::load(index_path, metadata.handle);
        ASSERT_TRUE(loaded);
        EXPECT_EQ(loaded->metadata, prepared.value().metadata);
        EXPECT_EQ(serializer.load(metadata_path(source)).value(), loaded->metadata);
        ASSERT_EQ(loaded->texture_samplers.size(), 3u);
        ASSERT_TRUE(database.scan().succeeded());
        for(const auto& child : loaded->metadata.subresources) {
            const auto* indexed = database.find(child.handle);
            ASSERT_NE(indexed, nullptr);
            EXPECT_EQ(indexed->source_asset, metadata.handle);
            const auto file = loaded->product_path(index_path, child.handle);
            if(child.type == AssetType::Mesh) {
                const auto mesh = MeshArtifact::load(file, child.handle);
                ASSERT_TRUE(mesh);
                EXPECT_EQ(mesh->data.indices.size(), 3u);
            } else if(child.type == AssetType::Material) {
                EXPECT_TRUE(MaterialSerializer{}.load(file));
            } else {
                const auto texture = TextureArtifact::load(file, child.handle, 4 * 1024 * 1024);
                ASSERT_TRUE(texture);
                EXPECT_EQ(texture->data.width, 512);
                EXPECT_EQ(texture->data.mip_levels, 10u);
                EXPECT_EQ(texture->data.format,
                    texture->settings.normal_map
                            || texture->settings.color_space == TextureColorSpace::Linear
                        ? Format::R8G8B8A8_UNORM
                        : Format::R8G8B8A8_SRGB);
                const auto& sampler = loaded->texture_samplers.at(child.handle);
                EXPECT_EQ(sampler.mag_filter, Filter::Nearest);
                EXPECT_EQ(sampler.address_mode_u, SamplerAddressMode::ClampToEdge);
            }
        }
        const auto stamp = std::filesystem::last_write_time(index_path);
        const auto reused = service.prepare_model(record, AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(reused);
        EXPECT_EQ(reused.value().generation, loaded->generation);
        EXPECT_EQ(std::filesystem::last_write_time(index_path), stamp);
        {
            std::ofstream output(geometry, std::ios::binary | std::ios::app);
            output.put('\0');
        }
        const auto changed = service.prepare_model(record, AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(changed) << changed.error();
        EXPECT_NE(changed.value().generation, loaded->generation);
        EXPECT_EQ(changed.value().metadata, loaded->metadata);
        EXPECT_EQ(
            changed.value().model.nodes[0].parts[0].mesh, loaded->model.nodes[0].parts[0].mesh);
        Scene scene;
        const auto part = loaded->model.nodes[0].parts[0];
        scene.create_entity("Imported part")
            .add_component<MeshRendererComponent>(
                MeshRendererComponent{.mesh = part.mesh, .material = part.material});
        const auto components = create_scene_component_registry();
        ASSERT_TRUE(SceneSerializer(components).save(scene, paths.assets() / "startup.scene"));
        ASSERT_TRUE(write_text_file_atomic(paths.root() / "project.json", R"({
            "version":2,"id":"00000000-0000-4000-8000-000000000002",
            "name":"Model preparation","startup_scene":"startup.scene",
            "input_contexts":[],"input_actions":[]})"));
        const auto project = Project::load(paths.root());
        ASSERT_TRUE(project);
        std::filesystem::remove_all(index_path.parent_path());
        const auto project_prepared = prepare_project(project.value());
        ASSERT_TRUE(project_prepared) << project_prepared.error();
        const auto rebuilt = ModelArtifact::load(index_path, metadata.handle);
        ASSERT_TRUE(rebuilt);
        EXPECT_EQ(rebuilt->metadata, loaded->metadata);
        EXPECT_EQ(rebuilt->generation, changed.value().generation);
        const auto image_path = paths.assets() / "surface.png";
        const auto image_bytes = read_binary_file(image_path, 1024 * 1024);
        ASSERT_TRUE(image_bytes);
        ASSERT_TRUE(write_text_file_atomic(image_path, "invalid image"));
        EXPECT_FALSE(service.prepare_model(record, AssetImportLimits{}.mesh_working_bytes));
        const auto unchanged = ModelArtifact::load(index_path, metadata.handle);
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(unchanged->generation, rebuilt->generation);
        EXPECT_EQ(serializer.load(metadata_path(source)).value(), rebuilt->metadata);
        ASSERT_TRUE(write_binary_file_atomic(image_path, image_bytes.value()));
        std::filesystem::resize_file(geometry, 2);
        EXPECT_FALSE(service.prepare_model(record, AssetImportLimits{}.mesh_working_bytes));
        EXPECT_FALSE(prepare_project(project.value()));
        EXPECT_EQ(serializer.load(metadata_path(source)).value(), loaded->metadata);
        const auto retained = ModelArtifact::load(index_path, metadata.handle);
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->generation, changed.value().generation);
    }

    TEST(ModelImporterTest, BackgroundCandidatePublishesOnlyOnOwnerAndRejectsChangedInputs) {
        const ModelInputs inputs;
        const auto original = inputs.model(ModelInputs::document(R"([{"mesh":0}])", MATERIALS));
        const ProjectPaths paths(inputs.directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto source = paths.assets() / "model.gltf";
        std::filesystem::copy_file(original, source);
        std::filesystem::copy_file(
            inputs.directory.path() / "geometry.bin", paths.assets() / "geometry.bin");
        const AssetMetadata metadata{.handle = AssetHandle{1234}, .type = AssetType::Model};
        const MetadataSerializer serializer;
        ASSERT_TRUE(serializer.save(metadata, metadata_path(source)));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        const ImportService service(paths);
        const auto index = service.model_artifact_path(metadata.handle);
        ASSERT_TRUE(manager.import_model_async(metadata.handle));
        scheduler.wait_idle();
        EXPECT_EQ(serializer.load(metadata_path(source)).value(), metadata);
        EXPECT_FALSE(std::filesystem::exists(index));
        EXPECT_EQ(manager.get_database().size(), 1u);
        EXPECT_TRUE(manager.is_import_pending(metadata.handle));
        const auto completed = manager.process_completions();
        ASSERT_TRUE(completed);
        EXPECT_EQ(completed.value(), std::vector<AssetHandle>{metadata.handle});
        EXPECT_FALSE(manager.is_import_pending(metadata.handle));
        ASSERT_TRUE(manager.load_model(metadata.handle));
        EXPECT_TRUE(std::filesystem::exists(index));
        const auto published_meta = serializer.load(metadata_path(source)).value();
        EXPECT_FALSE(published_meta.subresources.empty());
        EXPECT_TRUE(manager.source_imports_needing_recheck().empty());

        const auto record = *manager.get_database().find(metadata.handle);
        const auto candidate = service.stage_model(
            record, AssetImportLimits{}.mesh_working_bytes, AssetImportMode::Force);
        ASSERT_TRUE(candidate) << candidate.error();
        const auto published_bytes = read_binary_file(index, 4 * 1024 * 1024);
        ASSERT_TRUE(published_bytes);
        ASSERT_TRUE(write_text_file_atomic(source, "broken glTF"));
        EXPECT_FALSE(service.publish_model(record, candidate.value()));
        EXPECT_EQ(serializer.load(metadata_path(source)).value(), published_meta);
        EXPECT_EQ(read_binary_file(index, 4 * 1024 * 1024).value(), published_bytes.value());
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_model_async(metadata.handle));
        scheduler.wait_idle();
        const auto failed = manager.process_completions();
        ASSERT_TRUE(failed);
        EXPECT_TRUE(failed.value().empty());
        EXPECT_FALSE(manager.load_model(metadata.handle));
        EXPECT_EQ(
            manager.source_imports_needing_recheck(), std::vector<AssetHandle>{metadata.handle});

        std::filesystem::copy_file(
            original, source, std::filesystem::copy_options::overwrite_existing);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_model_async(metadata.handle));
        scheduler.wait_idle();
        // 删除发生在后台完成与主线程接纳之间；过期任务不能重建 .meta。
        std::filesystem::remove(source);
        std::filesystem::remove(metadata_path(source));
        ASSERT_TRUE(manager.scan().succeeded());
        const auto stale = manager.process_completions();
        ASSERT_TRUE(stale);
        EXPECT_TRUE(stale.value().empty());
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
    }

    TEST(ModelImporterTest, ModelCacheFollowsExplicitDemandAndOldViewsSurviveReleaseAndReload) {
        const ModelInputs inputs;
        const auto original = inputs.model(
            ModelInputs::document(R"([{"name":"Model","mesh":0}])", R"(,"materials":[{},{}])"));
        const ProjectPaths paths(inputs.directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto source = paths.assets() / "model.gltf";
        std::filesystem::copy_file(original, source);
        std::filesystem::copy_file(
            inputs.directory.path() / "geometry.bin", paths.assets() / "geometry.bin");
        const AssetHandle handle{1234};
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Model}, metadata_path(source)));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(manager.import_model(handle));
        const auto original_view = manager.load_model(handle);
        ASSERT_TRUE(original_view);
        ASSERT_FALSE(original_view.value()->nodes.empty());
        const auto mesh = original_view.value()->nodes[0].parts[0].mesh;
        EXPECT_EQ(manager.residency().model_cache_entries, 1u);
        const std::array child_demand{AssetReference{mesh, AssetType::Mesh, true, true}};
        EXPECT_EQ(manager.release_unused(child_demand).model_cache_entries, 0u);
        EXPECT_EQ(manager.residency().model_cache_entries, 1u);
        EXPECT_EQ(manager.release_unused({}).model_cache_entries, 1u);
        EXPECT_EQ(manager.residency().model_cache_entries, 0u);
        EXPECT_EQ(original_view.value()->nodes[0].parts[0].mesh, mesh);
        const auto reloaded = manager.load_model(handle);
        ASSERT_TRUE(reloaded) << reloaded.error().message;
        EXPECT_NE(reloaded.value(), original_view.value());
        EXPECT_EQ(reloaded.value()->nodes[0].parts[0].mesh, mesh);
        EXPECT_EQ(manager.residency().model_cache_entries, 1u);
        EXPECT_EQ(factory.mesh_creation_count(), 0u);
        EXPECT_EQ(factory.texture_creation_count(), 0u);
    }

    TEST(ModelImporterTest, RuntimeLoadsModelChildrenAndInvalidatesTheWholeModelOnSourceFailure) {
        const ModelInputs inputs;
        const auto original = inputs.model(
            ModelInputs::document(R"([{"name":"Model","mesh":0},{"name":"Other","mesh":1}])",
                R"(,"materials":[{"name":"Paint","pbrMetallicRoughness":{
                "baseColorTexture":{"index":0}},"normalTexture":{"index":0}},{}],
                "textures":[{"source":0,"sampler":0}],"images":[{"uri":"surface.png"}],
                "samplers":[{"magFilter":9728,"minFilter":9984,"wrapS":33071,"wrapT":33648}])"));
        const ProjectPaths paths(inputs.directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto source = paths.assets() / "model.gltf";
        std::filesystem::copy_file(original, source);
        std::filesystem::copy_file(
            inputs.directory.path() / "geometry.bin", paths.assets() / "geometry.bin");
        const auto image = paths.assets() / "surface.png";
        std::filesystem::copy_file(std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY)
                                       / "assets/textures/awesomeface.png",
            image);
        const AssetHandle model{1234};
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = model, .type = AssetType::Model}, metadata_path(source)));
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler(1);
        AssetManager manager(paths, registry, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_FALSE(manager.load_model(model));
        const auto imported = manager.import_model(model);
        ASSERT_TRUE(imported) << imported.error().message;
        const auto loaded = manager.load_model(model);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(loaded.value(), manager.load_model(model).value());
        const std::array model_reference{AssetReference{model, AssetType::Model}};
        ASSERT_TRUE(manager.ensure_loaded(model, AssetType::Model));
        const auto ready = manager.references_ready(
            model_reference, AssetManager::MissingAssetPolicy::FailRequired);
        ASSERT_TRUE(ready);
        EXPECT_TRUE(ready.value());
        const auto part = loaded.value()->nodes[0].parts[0];
        const auto cpu_mesh = manager.read_mesh_data(part.mesh);
        ASSERT_TRUE(cpu_mesh) << cpu_mesh.error().message;
        ASSERT_EQ(cpu_mesh.value().vertices.size(), 3u);
        EXPECT_EQ(cpu_mesh.value().vertices[1].position, Math::Vec3(1, 0, 0));
        EXPECT_EQ(cpu_mesh.value().indices, (std::vector<uint32_t>{0, 1, 2}));
        EXPECT_EQ(factory.mesh_creation_count(), 0u);
        EXPECT_EQ(factory.texture_creation_count(), 0u);
        EXPECT_FALSE(registry.contains(part.mesh));
        EXPECT_FALSE(manager.read_mesh_data(model));
        const auto mesh = manager.load_mesh(part.mesh);
        ASSERT_TRUE(mesh) << mesh.error().message;
        const auto material = manager.load_material(part.material);
        ASSERT_TRUE(material) << material.error().message;
        EXPECT_TRUE(material.value()->get_texture_property("base_color_texture"));
        EXPECT_TRUE(material.value()->get_texture_property("normal_texture"));
        EXPECT_EQ(factory.mesh_creation_count(), 1u);
        EXPECT_EQ(factory.texture_creation_count(), 2u);
        ASSERT_TRUE(factory.last_texture_sampler());
        EXPECT_EQ(factory.last_texture_sampler()->mag_filter, Filter::Nearest);
        EXPECT_EQ(factory.last_texture_sampler()->address_mode_u, SamplerAddressMode::ClampToEdge);
        EXPECT_EQ(factory.last_texture_sampler()->address_mode_v, SamplerAddressMode::MirrorRepeat);
        ASSERT_TRUE(manager.import_mesh(part.mesh));
        EXPECT_EQ(manager.load_mesh(part.mesh).value(), mesh.value());
        EXPECT_EQ(manager.load_material(part.material).value(), material.value());
        EXPECT_EQ(factory.texture_creation_count(), 2u);
        const auto metadata = MetadataSerializer{}.load(metadata_path(source));
        ASSERT_TRUE(metadata);
        const auto texture = std::ranges::find(
            metadata.value().subresources, AssetType::Texture, &AssetSubresource::type)
                                 ->handle;
        EXPECT_FALSE(manager.reimport_texture(texture, {}));
        EXPECT_FALSE(manager.prepare_material_update(part.material, {.template_name = "pbr"}));
        EXPECT_EQ(MetadataSerializer{}.load(metadata_path(source)).value(), metadata.value());

        const auto image_bytes = read_binary_file(image, 4 * 1024 * 1024);
        ASSERT_TRUE(image_bytes);
        ASSERT_TRUE(write_text_file_atomic(image, "corrupt image"));
        EXPECT_FALSE(manager.import_model(model));
        EXPECT_FALSE(registry.contains(part.mesh));
        EXPECT_FALSE(registry.contains(part.material));
        EXPECT_FALSE(registry.contains(texture));
        EXPECT_FALSE(manager.load_model(model));
        EXPECT_FALSE(manager.load_mesh(part.mesh));
        EXPECT_FALSE(manager.read_mesh_data(part.mesh));
        EXPECT_FALSE(manager.load_material(part.material));
        // 已提交帧可保留旧对象，后续帧不能继续解析到旧版本。
        EXPECT_TRUE(mesh.value());
        EXPECT_TRUE(material.value());
        ASSERT_TRUE(write_binary_file_atomic(image, image_bytes.value()));
        ASSERT_TRUE(manager.import_model(model));
        const auto restored = manager.load_model(model);
        ASSERT_TRUE(restored);
        EXPECT_EQ(restored.value()->nodes[0].parts[0].mesh, part.mesh);
        EXPECT_EQ(restored.value()->nodes[0].parts[0].material, part.material);
        const auto refreshed = manager.load_material(part.material);
        ASSERT_TRUE(refreshed);
        EXPECT_NE(refreshed.value(), material.value());
        EXPECT_EQ(factory.texture_creation_count(), 4u);
        const auto rebuilt_mesh = manager.load_mesh(part.mesh);
        ASSERT_TRUE(rebuilt_mesh);
        EXPECT_NE(rebuilt_mesh.value(), mesh.value());
        {
            std::ofstream changed(
                paths.assets() / "geometry.bin", std::ios::binary | std::ios::app);
            changed.put('x');
        }
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_FALSE(registry.contains(part.mesh));
        EXPECT_FALSE(registry.contains(part.material));
        EXPECT_FALSE(manager.load_mesh(part.mesh));
        EXPECT_FALSE(manager.load_material(part.material));
        EXPECT_FALSE(manager.references_ready(
            model_reference, AssetManager::MissingAssetPolicy::FailRequired));
        ASSERT_TRUE(manager.import_model(model));
        EXPECT_TRUE(manager.load_mesh(part.mesh));
        EXPECT_TRUE(manager.load_material(part.material));
    }

    TEST(ModelImporterTest, ImportsBuiltInCharacterHierarchyAndAnimation) {
        const auto path =
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/resources/characters/default.glb";
        const auto imported = ModelImporter{}.import(path);
        ASSERT_TRUE(imported) << imported.error();
        ASSERT_FALSE(imported.value().meshes.empty());
        for(const auto& mesh : imported.value().meshes) {
            ASSERT_TRUE(mesh.data.animation);
            EXPECT_FALSE(mesh.data.animation->clips.empty());
            EXPECT_FALSE(mesh.data.animation->joints.empty());
            EXPECT_EQ(mesh.data.skin.size(), mesh.data.vertices.size());
        }
    }

    TEST(ModelImporterTest, RejectsInvalidNodeRotationAndMaterialFactors) {
        const ModelInputs inputs;
        EXPECT_FALSE(ModelImporter{}.import(inputs.model(
            ModelInputs::document(R"([{"mesh":0,"rotation":[0,0,0,0]}])", MATERIALS))));
        for(const auto material : {R"({"pbrMetallicRoughness":{"metallicFactor":2}})",
                R"({"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,-1]}})",
                R"({"occlusionTexture":{"index":0,"strength":-1}})"}) {
            const std::string extra =
                R"(,"materials":[)" + std::string(material)
                + R"(,{}],"textures":[{"source":0}],"images":[{"uri":"image.png"}])";
            inputs.write("image.png", "image");
            const auto source = inputs.model(ModelInputs::document(R"([{"mesh":0}])", extra));
            const auto imported = ModelImporter{}.import(source);
            ASSERT_FALSE(imported);
        }
    }
    TEST(ModelImporterTest,
        DownloadedMultiMaterialModelUpdatesBothDemoLevelsWithoutLosingOverrides) {
        const std::filesystem::path demo(COMET_SAMPLE_PROJECT_DIRECTORY);
        const auto source = demo / "assets/models/milk_truck.glb";
        const auto metadata = MetadataSerializer{}.load(metadata_path(source));
        ASSERT_TRUE(metadata) << metadata.error();
        const TemporaryDirectory directory;
        const ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto copied = paths.assets() / "milk_truck.glb";
        std::filesystem::copy_file(source, copied);
        ASSERT_TRUE(MetadataSerializer{}.save(metadata.value(), metadata_path(copied)));
        AssetDatabase database(paths);
        ASSERT_TRUE(database.scan().succeeded());
        const auto* record = database.find(metadata.value().handle);
        ASSERT_NE(record, nullptr);
        const ImportService imports(paths);
        const auto initial = imports.prepare_model(*record, AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(initial) << initial.error();
        EXPECT_EQ(initial.value().metadata, metadata.value());
        EXPECT_EQ(initial.value().model.nodes.size(), 6u);
        const auto count = [&](AssetType type) {
            return std::ranges::count_if(metadata.value().subresources,
                [&](const auto& child) { return child.type == type; });
        };
        EXPECT_EQ(count(AssetType::Material), 4);
        EXPECT_EQ(count(AssetType::Mesh), 4);
        EXPECT_EQ(count(AssetType::Texture), 2);

        // 修改真实 GLB 的车轮支架位置，保持 JSON 长度和所有节点名不变。
        auto bytes = read_binary_file(copied, 1024 * 1024);
        ASSERT_TRUE(bytes);
        const std::string_view document(
            reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
        constexpr std::string_view prefix = R"("translation":[)";
        const auto position = document.find(R"("translation":[1.432669997215271)");
        ASSERT_NE(position, std::string_view::npos);
        bytes.value()[position + prefix.size()] = std::byte{'2'};
        ASSERT_TRUE(write_binary_file_atomic(copied, bytes.value()));
        ASSERT_TRUE(database.scan().succeeded());
        record = database.find(metadata.value().handle);
        ASSERT_NE(record, nullptr);
        const auto changed = imports.prepare_model(*record, AssetImportLimits{}.mesh_working_bytes);
        ASSERT_TRUE(changed) << changed.error();
        EXPECT_NE(changed.value().generation, initial.value().generation);
        EXPECT_EQ(changed.value().metadata, initial.value().metadata);

        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto incoming = Prefab::from_model(
            changed.value().model, metadata.value().handle, registry, "牛奶货车");
        ASSERT_TRUE(incoming) << incoming.error();
        const auto named = [](Scene& scene, std::string_view name) {
            for(auto entity : scene.get_entities())
                if(entity.get_component<NameComponent>().name == name)
                    return entity;
            return Entity{};
        };
        std::vector<EntityUuid> roots;
        for(const auto* name : {"character.scene", "character_second.scene"}) {
            SCOPED_TRACE(name);
            auto loaded = load_demo_scene(registry, std::filesystem::path("scenes") / name);
            ASSERT_TRUE(loaded) << loaded.error();
            auto& scene = *loaded.value();
            const auto root = named(scene, "牛奶货车");
            const auto support = named(scene, "Node");
            ASSERT_TRUE(root);
            ASSERT_TRUE(support);
            roots.push_back(root.get_uuid());
            const auto placement = root.get_component<TransformComponent>();
            const auto identities = root.get_component<PrefabInstanceComponent>().entities;
            EXPECT_EQ(root.get_component<PrefabInstanceComponent>().asset, metadata.value().handle);
            // 两关已有不同的放置角度；额外的节点旋转与材质覆盖也须保留。
            support.edit_transform([](auto& transform) { transform.rotation.z = 17; });
            const auto wheel = named(scene, "Wheels");
            ASSERT_TRUE(wheel);
            const auto primitive = scene.get_children(wheel).front();
            primitive.edit_component<MeshRendererComponent>(
                [](auto& renderer) { renderer.material = DEFAULT_PBR_MATERIAL; });
            auto updated = incoming.value().update_candidate(scene, registry, root);
            ASSERT_TRUE(updated) << updated.error();
            Scene merged;
            ASSERT_TRUE(EntityTrees::restore(merged, registry, updated.value()));
            const auto kept_root = merged.find_entity(root.get_uuid());
            const auto kept_support = merged.find_entity(support.get_uuid());
            ASSERT_TRUE(kept_root);
            ASSERT_TRUE(kept_support);
            const auto& kept_placement = kept_root.get_component<TransformComponent>();
            EXPECT_EQ(kept_placement.translation, placement.translation);
            EXPECT_EQ(kept_placement.rotation, placement.rotation);
            EXPECT_EQ(kept_placement.scale, placement.scale);
            EXPECT_EQ(kept_root.get_component<PrefabInstanceComponent>().entities, identities);
            const auto transform = kept_support.get_component<TransformComponent>();
            EXPECT_NEAR(transform.translation.x, 2.43267f, 0.00001f);
            EXPECT_EQ(transform.rotation.z, 17);
            EXPECT_EQ(merged.find_entity(primitive.get_uuid())
                          .get_component<MeshRendererComponent>()
                          .material,
                DEFAULT_PBR_MATERIAL);
            const auto serialized = serializer.serialize(merged);
            ASSERT_TRUE(serialized);
            auto reopened = serializer.deserialize(serialized.value());
            ASSERT_TRUE(reopened);
            auto reupdated = incoming.value().update_candidate(
                *reopened.value(), registry, reopened.value()->find_entity(root.get_uuid()));
            ASSERT_TRUE(reupdated) << reupdated.error();
            EXPECT_EQ(reupdated.value().size(), updated.value().size());
        }
        ASSERT_EQ(roots.size(), 2u);
        EXPECT_NE(roots[0], roots[1]);
    }

}
