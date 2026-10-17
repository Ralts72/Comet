#include "asset/import/model_importer.h"
#include "asset/import/terrain_importer.h"
#include "asset/import/gltf_mesh.h"
#include "common/file_io.h"
#include "asset/serialization/metadata_serializer.h"

#include <fastgltf/core.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <span>
#include <tuple>
#include <unordered_set>

namespace Comet {
    namespace {
        using Model = ModelImportData;
        struct Plan {
            fastgltf::Asset asset;
            Model model;
            std::vector<GltfMeshSelection> selections;
            std::map<std::filesystem::path, std::size_t> file_sizes;
            std::size_t source_bytes = 0;
            std::size_t working_bytes = 0;
        };

        Result<fastgltf::Asset> parse(const std::filesystem::path& path) {
            auto source = fastgltf::GltfDataBuffer::FromPath(path);
            if(!source)
                return Result<fastgltf::Asset>::failure(
                    std::string(fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_emissive_strength
                                    | fastgltf::Extensions::KHR_texture_transform);
            auto options = fastgltf::Options::DecomposeNodeMatrices;
            auto parsed =
                parser.loadGltf(source.get(), path.parent_path(), options, fastgltf::Category::All);
            if(!parsed)
                return Result<fastgltf::Asset>::failure(
                    std::string(fastgltf::getErrorMessage(parsed.error())));
            const auto valid = fastgltf::validate(parsed.get());
            if(valid != fastgltf::Error::None)
                return Result<fastgltf::Asset>::failure(
                    std::string(fastgltf::getErrorMessage(valid)));
            return Result<fastgltf::Asset>::success(std::move(parsed.get()));
        }

        Result<void> plan_nodes(Plan& plan) {
            const auto& asset = plan.asset;
            auto& model = plan.model;
            if(asset.nodes.size() >= Model::NONE || asset.meshes.size() >= Model::NONE)
                return Result<void>::failure("Model has too many nodes or meshes");
            model.nodes.resize(asset.nodes.empty() ? asset.meshes.size() : asset.nodes.size());
            std::map<std::tuple<size_t, size_t, size_t>, uint32_t> meshes;
            const auto bind_mesh = [&](uint32_t node, size_t mesh,
                                       std::optional<size_t> skin_node) {
                const auto& source = asset.meshes[mesh];
                for(size_t primitive = 0; primitive < source.primitives.size(); ++primitive) {
                    const auto key = std::tuple{mesh, primitive, skin_node.value_or(Model::NONE)};
                    const auto [entry, added] =
                        meshes.try_emplace(key, uint32_t(model.meshes.size()));
                    if(added) {
                        const auto material = source.primitives[primitive].materialIndex;
                        model.meshes.push_back({.name = std::string(source.name),
                            .source_mesh = uint32_t(mesh),
                            .source_primitive = uint32_t(primitive),
                            .skin_node = skin_node,
                            .material = material ? uint32_t(*material) : Model::NONE});
                        plan.selections.push_back({mesh, primitive, skin_node});
                    }
                    model.nodes[node].meshes.push_back(entry->second);
                }
            };
            if(asset.nodes.empty()) {
                for(uint32_t mesh = 0; mesh < asset.meshes.size(); ++mesh) {
                    model.nodes[mesh].name = asset.meshes[mesh].name;
                    bind_mesh(mesh, mesh, {});
                }
            }
            for(uint32_t index = 0; index < asset.nodes.size(); ++index) {
                const auto& source = asset.nodes[index];
                auto& node = model.nodes[index];
                node.name = source.name;
                const auto* trs = std::get_if<fastgltf::TRS>(&source.transform);
                if(!trs)
                    return Result<void>::failure("Cannot decompose glTF node transform");
                node.transform.translation = {
                    trs->translation[0], trs->translation[1], trs->translation[2]};
                node.transform.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
                node.transform.rotation = {
                    trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
                if(!Math::is_finite(node.transform.translation)
                    || !Math::is_finite(node.transform.scale)
                    || !Math::is_finite(Math::Vec4(
                        trs->rotation[0], trs->rotation[1], trs->rotation[2], trs->rotation[3])))
                    return Result<void>::failure("Model node transform is not finite");
                const auto rotation_length = glm::length(node.transform.rotation);
                if(std::abs(rotation_length - 1.f) > 1e-3f)
                    return Result<void>::failure("Model node rotation must be a unit quaternion");
                node.transform.rotation /= rotation_length;
                for(const auto child : source.children) {
                    if(child >= model.nodes.size() || model.nodes[child].parent != Model::NONE)
                        return Result<void>::failure("Model nodes must form a tree");
                    model.nodes[child].parent = index;
                }
                if(source.meshIndex)
                    bind_mesh(index, *source.meshIndex,
                        source.skinIndex ? std::optional<size_t>(index) : std::nullopt);
            }
            // 迭代遍历，避免深层外部模型占用递归栈。
            std::vector<uint32_t> order;
            for(uint32_t index = 0; index < model.nodes.size(); ++index)
                if(model.nodes[index].parent == Model::NONE)
                    order.push_back(index);
            const auto roots = order;
            for(size_t cursor = 0; cursor < order.size() && !asset.nodes.empty(); ++cursor)
                for(const auto child : asset.nodes[order[cursor]].children)
                    order.push_back(uint32_t(child));
            if(order.size() != model.nodes.size())
                return Result<void>::failure("Model node hierarchy contains a cycle");
            for(const auto& scene : asset.scenes) {
                Model::Scene result{.name = std::string(scene.name)};
                for(const auto root : scene.nodeIndices) {
                    if(root >= model.nodes.size() || model.nodes[root].parent != Model::NONE
                        || std::ranges::find(result.roots, uint32_t(root)) != result.roots.end())
                        return Result<void>::failure("Invalid model scene root");
                    result.roots.push_back(uint32_t(root));
                }
                model.scenes.push_back(std::move(result));
            }
            if(model.scenes.empty())
                model.scenes.push_back({"Scene", roots});
            model.default_scene = uint32_t(asset.defaultScene.value_or(0));
            return Result<void>::success();
        }

        SamplerAddressMode wrap(fastgltf::Wrap value) {
            switch(value) {
                case fastgltf::Wrap::ClampToEdge:
                    return SamplerAddressMode::ClampToEdge;
                case fastgltf::Wrap::MirroredRepeat:
                    return SamplerAddressMode::MirrorRepeat;
                default:
                    return SamplerAddressMode::Repeat;
            }
        }

        Result<void> plan_materials(Plan& plan) {
            for(const auto& source : plan.asset.textures) {
                if(!source.imageIndex)
                    return Result<void>::failure("Texture has no supported glTF image");
                Model::Texture texture{
                    .name = std::string(source.name), .image = uint32_t(*source.imageIndex)};
                if(source.samplerIndex) {
                    const auto& sampler = plan.asset.samplers[*source.samplerIndex];
                    texture.sampler.mag_filter = sampler.magFilter == fastgltf::Filter::Nearest
                                                     ? Filter::Nearest
                                                     : Filter::Linear;
                    const auto min =
                        sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear);
                    texture.sampler.min_filter =
                        min == fastgltf::Filter::Nearest
                                || min == fastgltf::Filter::NearestMipMapNearest
                                || min == fastgltf::Filter::NearestMipMapLinear
                            ? Filter::Nearest
                            : Filter::Linear;
                    texture.sampler.mipmap_mode =
                        min == fastgltf::Filter::NearestMipMapNearest
                                || min == fastgltf::Filter::LinearMipMapNearest
                            ? SamplerMipmapMode::Nearest
                            : SamplerMipmapMode::Linear;
                    texture.mipmaps =
                        min != fastgltf::Filter::Nearest && min != fastgltf::Filter::Linear;
                    texture.sampler.address_mode_u = wrap(sampler.wrapS);
                    texture.sampler.address_mode_v = wrap(sampler.wrapT);
                }
                plan.model.textures.push_back(std::move(texture));
            }
            for(const auto& source : plan.asset.materials) {
                Model::Material material{.name = std::string(source.name)};
                auto& data = material.data;
                data.template_name = "pbr";
                const auto& pbr = source.pbrData;
                data.vector_properties["base_color"] = {pbr.baseColorFactor[0],
                    pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]};
                data.vector_properties["emissive_color"] = {source.emissiveFactor[0],
                    source.emissiveFactor[1], source.emissiveFactor[2], 1};
                data.scalar_properties = {{"metallic", float(pbr.metallicFactor)},
                    {"roughness", float(pbr.roughnessFactor)},
                    {"emissive_strength", float(source.emissiveStrength)},
                    {"normal_scale",
                        source.normalTexture ? float(source.normalTexture->scale) : 1.f},
                    {"occlusion_strength",
                        source.occlusionTexture ? float(source.occlusionTexture->strength) : 1.f}};
                const auto unit_factor = [](std::initializer_list<float> values) {
                    return std::ranges::all_of(values, [](float value) {
                        return std::isfinite(value) && value >= 0.f && value <= 1.f;
                    });
                };
                if(!unit_factor({pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                       pbr.baseColorFactor[2], pbr.baseColorFactor[3]})
                    || !unit_factor({source.emissiveFactor[0], source.emissiveFactor[1],
                        source.emissiveFactor[2]})
                    || !unit_factor({float(pbr.metallicFactor), float(pbr.roughnessFactor),
                        data.scalar_properties.at("occlusion_strength")})
                    || !std::isfinite(source.emissiveStrength) || source.emissiveStrength < 0.f
                    || !std::isfinite(data.scalar_properties.at("normal_scale")))
                    return Result<void>::failure("Invalid model PBR material factors");
                switch(source.alphaMode) {
                    case fastgltf::AlphaMode::Opaque:
                        data.surface.alpha_mode = AlphaMode::Opaque;
                        break;
                    case fastgltf::AlphaMode::Mask:
                        data.surface.alpha_mode = AlphaMode::Mask;
                        break;
                    case fastgltf::AlphaMode::Blend:
                        data.surface.alpha_mode = AlphaMode::Blend;
                        break;
                }
                data.surface.alpha_cutoff = float(source.alphaCutoff);
                data.surface.double_sided = source.doubleSided;
                if(!data.surface.valid())
                    return Result<void>::failure("Invalid model material surface");
                const auto bind = [&](std::string property, const auto& info,
                                      TextureColorSpace space,
                                      bool normal = false) -> Result<void> {
                    if(!info)
                        return Result<void>::success();
                    if(info->texCoordIndex != 0 || info->transform)
                        return Result<void>::failure(
                            "Model textures currently require TEXCOORD_0 without KHR_texture_transform");
                    TextureImportSettings settings;
                    settings.color_space = space;
                    settings.normal_map = normal;
                    settings.generate_mipmaps = plan.model.textures[info->textureIndex].mipmaps;
                    material.textures.push_back(
                        {std::move(property), uint32_t(info->textureIndex), settings});
                    return Result<void>::success();
                };
                if(auto r =
                        bind("base_color_texture", pbr.baseColorTexture, TextureColorSpace::Srgb);
                    !r)
                    return r;
                if(auto r = bind("metallic_roughness_texture", pbr.metallicRoughnessTexture,
                       TextureColorSpace::Linear);
                    !r)
                    return r;
                if(auto r = bind(
                       "normal_texture", source.normalTexture, TextureColorSpace::Linear, true);
                    !r)
                    return r;
                if(auto r = bind(
                       "occlusion_texture", source.occlusionTexture, TextureColorSpace::Linear);
                    !r)
                    return r;
                if(auto r =
                        bind("emissive_texture", source.emissiveTexture, TextureColorSpace::Srgb);
                    !r)
                    return r;
                plan.model.materials.push_back(std::move(material));
            }
            return Result<void>::success();
        }

        Result<Plan> inspect(const std::filesystem::path& path, const AssetImportLimits& limits) {
            std::error_code error;
            const auto bytes = std::filesystem::file_size(path, error);
            if(error || bytes > limits.source_bytes
                || limits.mesh_working_bytes < GLTF_IMPORT_OVERHEAD_BYTES
                || bytes > (limits.mesh_working_bytes - GLTF_IMPORT_OVERHEAD_BYTES) / 3)
                return Result<Plan>::failure("Model source exceeds its input or working budget");
            auto parsed = parse(path);
            if(!parsed)
                return Result<Plan>::failure(parsed.error());
            Plan plan{.asset = std::move(parsed).value(), .source_bytes = size_t(bytes)};
            const auto add_source = [&](const fastgltf::DataSource& data) -> Result<void> {
                const auto* uri = std::get_if<fastgltf::sources::URI>(&data);
                if(!uri)
                    return Result<void>::success();
                if(!uri->uri.isLocalPath() || uri->fileByteOffset != 0)
                    return Result<void>::failure(
                        "Model source must use local, complete companion files");
                const auto source = (path.parent_path() / uri->uri.fspath()).lexically_normal();
                if(std::ranges::find(plan.model.source_dependencies, source)
                    != plan.model.source_dependencies.end())
                    return Result<void>::success();
                const auto size = std::filesystem::file_size(source, error);
                if(error || size > limits.source_bytes - plan.source_bytes)
                    return Result<void>::failure(
                        "Cannot read model companion file or source budget exceeded: "
                        + source.string());
                plan.source_bytes += size;
                plan.file_sizes[source] = std::size_t(size);
                plan.model.source_dependencies.push_back(source);
                return Result<void>::success();
            };
            for(const auto& buffer : plan.asset.buffers)
                if(auto added = add_source(buffer.data); !added)
                    return Result<Plan>::failure(added.error());
            for(const auto& image : plan.asset.images)
                if(auto added = add_source(image.data); !added)
                    return Result<Plan>::failure(added.error());
            if(plan.source_bytes > (limits.mesh_working_bytes - GLTF_IMPORT_OVERHEAD_BYTES) / 3)
                return Result<Plan>::failure("Model sources exceed working budget");
            if(auto nodes = plan_nodes(plan); !nodes)
                return Result<Plan>::failure(nodes.error());
            if(auto materials = plan_materials(plan); !materials)
                return Result<Plan>::failure(materials.error());
            const auto base = GLTF_IMPORT_OVERHEAD_BYTES + plan.source_bytes * 3;
            plan.working_bytes = base;
            for(const auto& selection : plan.selections) {
                const auto estimate =
                    estimate_gltf_mesh(plan.asset, selection, plan.source_bytes, limits);
                if(!estimate)
                    return Result<Plan>::failure(estimate.error());
                const auto mesh_bytes = estimate.value() - base;
                if(mesh_bytes > limits.mesh_working_bytes - plan.working_bytes)
                    return Result<Plan>::failure("Model meshes exceed working budget");
                plan.working_bytes += mesh_bytes;
            }
            const auto reserve = [&](std::size_t count, std::size_t stride) {
                if(count > (limits.mesh_working_bytes - plan.working_bytes) / stride)
                    return false;
                plan.working_bytes += count * stride;
                return true;
            };
            if(!reserve(plan.model.nodes.size(), 256) || !reserve(plan.model.meshes.size(), 128)
                || !reserve(plan.model.materials.size(), 2048)
                || !reserve(plan.model.textures.size(), 128))
                return Result<Plan>::failure("Model structure exceeds working budget");
            for(const auto& image : plan.asset.images) {
                std::size_t count = 0;
                if(const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
                    count = plan.file_sizes.at(
                        (path.parent_path() / uri->uri.fspath()).lexically_normal());
                else if(const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data))
                    count = plan.asset.bufferViews[view->bufferViewIndex].byteLength;
                else if(const auto* array = std::get_if<fastgltf::sources::Array>(&image.data))
                    count = array->bytes.size();
                if(!reserve(count, 1))
                    return Result<Plan>::failure("Model images exceed working budget");
            }
            return Result<Plan>::success(std::move(plan));
        }

        std::span<const std::byte> bytes(const fastgltf::DataSource& source) {
            if(const auto* array = std::get_if<fastgltf::sources::Array>(&source))
                return {array->bytes.data(), array->bytes.size()};
            if(const auto* vector = std::get_if<fastgltf::sources::Vector>(&source))
                return vector->bytes;
            if(const auto* view = std::get_if<fastgltf::sources::ByteView>(&source))
                return {view->bytes.data(), view->bytes.size()};
            return {};
        }

        Result<std::vector<std::byte>> image_bytes(const fastgltf::Asset& asset,
            const fastgltf::Image& image, const std::filesystem::path& root, std::size_t limit) {
            if(const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
                auto content = read_binary_file(root / uri->uri.fspath(), limit);
                if(content && content.value().empty())
                    return Result<std::vector<std::byte>>::failure("Model image is empty");
                return content;
            }
            auto content = bytes(image.data);
            if(const auto* source = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
                const auto& view = asset.bufferViews[source->bufferViewIndex];
                content = bytes(asset.buffers[view.bufferIndex].data);
                if(view.byteOffset > content.size()
                    || view.byteLength > content.size() - view.byteOffset)
                    return Result<std::vector<std::byte>>::failure(
                        "Model image exceeds buffer bounds");
                content = content.subspan(view.byteOffset, view.byteLength);
            }
            if(content.empty() || content.size() > limit)
                return Result<std::vector<std::byte>>::failure(
                    "Model image is empty or exceeds input budget");
            return Result<std::vector<std::byte>>::success({content.begin(), content.end()});
        }
    }

    Result<ModelImporter::SourceInfo> ModelImporter::inspect_source(
        const std::filesystem::path& source, const AssetImportLimits& limits) {
        if(source_extension(source) == ".terrain")
            return TerrainImporter::inspect_source(source, limits);
        auto plan = inspect(source, limits);
        if(!plan)
            return Result<SourceInfo>::failure(
                "Cannot inspect model '" + source.string() + "': " + plan.error());
        return Result<SourceInfo>::success(
            {plan.value().working_bytes, std::move(plan.value().model.source_dependencies)});
    }

    Result<std::size_t> ModelImporter::working_bytes(
        const std::filesystem::path& source, const AssetImportLimits& limits) {
        auto info = inspect_source(source, limits);
        if(!info)
            return Result<std::size_t>::failure(info.error());
        return Result<std::size_t>::success(info.value().working_bytes);
    }

    Result<ModelImportData> ModelImporter::import(const std::filesystem::path& source,
        std::size_t memory_budget, const AssetImportLimits& limits) const {
        if(source_extension(source) == ".terrain")
            return TerrainImporter{}.import(source, memory_budget, limits);
        const auto failure = [&source](const std::string& message) {
            return Result<Model>::failure(
                "Cannot import model '" + source.string() + "': " + message);
        };
        auto candidate = inspect(source, limits);
        if(!candidate)
            return failure(candidate.error());
        auto plan = std::move(candidate).value();
        if(plan.working_bytes > memory_budget)
            return failure("Model exceeds its reserved working budget");
        // 使用已解析的同一份描述，伴随缓冲按 inspect 的实际文件大小限额读取。
        for(auto& buffer : plan.asset.buffers) {
            const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data);
            if(!uri)
                continue;
            const auto path = (source.parent_path() / uri->uri.fspath()).lexically_normal();
            auto content = read_binary_file(path, plan.file_sizes.at(path));
            if(!content)
                return failure(content.error());
            if(content.value().size() < buffer.byteLength)
                return failure("Model buffer is shorter than its declared size");
            buffer.data = fastgltf::sources::Vector{std::move(content).value()};
        }
        for(size_t index = 0; index < plan.selections.size(); ++index) {
            auto mesh = import_gltf_mesh(plan.asset, plan.selections[index], source);
            if(!mesh)
                return failure(mesh.error());
            plan.model.meshes[index].data = std::move(mesh).value();
        }
        for(const auto& image : plan.asset.images) {
            auto image_limit = limits.source_bytes;
            if(const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
                image_limit = plan.file_sizes.at(
                    (source.parent_path() / uri->uri.fspath()).lexically_normal());
            auto encoded = image_bytes(plan.asset, image, source.parent_path(), image_limit);
            if(!encoded)
                return failure(encoded.error());
            std::string name(image.name);
            if(name.empty()) {
                if(const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
                    name = uri->uri.fspath().generic_string();
            }
            plan.model.images.push_back({std::move(name), std::move(encoded).value()});
        }
        return Result<Model>::success(std::move(plan.model));
    }

    Result<ModelImportProduct> ModelImporter::bind(
        ModelImportData imported, const AssetMetadata& previous) {
        using Bound = Result<ModelImportProduct>;
        if(previous.type != AssetType::Model)
            return Bound::failure("Expected model metadata");
        if(auto valid = MetadataSerializer{}.serialize(previous); !valid)
            return Bound::failure(valid.error());
        ModelImportProduct result;
        result.metadata = previous;
        result.metadata.subresources.clear();
        result.model.default_scene = imported.default_scene;
        std::map<std::string, AssetSubresource> old_resources;
        std::unordered_set<AssetHandle> used_handles{previous.handle};
        for(const auto& resource : previous.subresources) {
            old_resources.emplace(resource.key, resource);
            used_handles.insert(resource.handle);
        }
        std::map<std::string, AssetHandle> assigned;
        const auto assign = [&](const std::string& key, AssetType type) -> Result<AssetHandle> {
            if(const auto existing = assigned.find(key); existing != assigned.end())
                return Result<AssetHandle>::success(existing->second);
            AssetHandle handle;
            if(const auto old = old_resources.find(key); old != old_resources.end()) {
                if(old->second.type != type)
                    return Result<AssetHandle>::failure("Model subresource changed type: " + key);
                handle = old->second.handle;
            } else {
                do {
                    handle = AssetHandle::generate();
                } while(!used_handles.insert(handle).second);
            }
            result.metadata.subresources.push_back({key, handle, type});
            assigned.emplace(key, handle);
            return Result<AssetHandle>::success(handle);
        };
        // 唯一名称不依赖导出下标；无名或重名对象以源下标区分，避免串用身份。
        const auto keys = [](const auto& objects) {
            std::map<std::string, size_t> names;
            for(const auto& object : objects)
                ++names[object.name];
            std::vector<std::string> result;
            for(size_t index = 0; index < objects.size(); ++index) {
                const auto& name = objects[index].name;
                if(!name.empty() && names[name] == 1)
                    result.push_back("name/" + std::to_string(name.size()) + ":" + name);
                else
                    result.push_back("index/" + std::to_string(index));
            }
            return result;
        };
        const auto node_keys = keys(imported.nodes);
        const auto material_keys = keys(imported.materials);
        auto texture_names = imported.textures;
        for(auto& texture : texture_names)
            if(texture.name.empty() && texture.image < imported.images.size())
                texture.name = imported.images[texture.image].name;
        const auto texture_keys = keys(texture_names);
        std::map<uint32_t, std::string> mesh_names;
        std::map<std::string, size_t> mesh_name_counts;
        for(const auto& mesh : imported.meshes)
            mesh_names.try_emplace(mesh.source_mesh, mesh.name);
        for(const auto& [index, name] : mesh_names)
            ++mesh_name_counts[name];
        std::unordered_set<AssetHandle> textures;
        std::vector<AssetHandle> material_handles;
        for(size_t index = 0; index < imported.materials.size(); ++index) {
            auto& material = imported.materials[index];
            const auto handle = assign("material/" + material_keys[index], AssetType::Material);
            if(!handle)
                return Bound::failure(handle.error());
            for(const auto& binding : material.textures) {
                if(binding.texture >= imported.textures.size())
                    return Bound::failure("Model material references an invalid texture");
                const auto& texture = imported.textures[binding.texture];
                if(texture.image >= imported.images.size())
                    return Bound::failure("Model texture references an invalid image");
                const char* role = "linear";
                if(binding.settings.normal_map)
                    role = "normal";
                else if(binding.settings.color_space == TextureColorSpace::Srgb)
                    role = "srgb";
                const auto texture_handle = assign(
                    "texture/" + texture_keys[binding.texture] + "/" + role, AssetType::Texture);
                if(!texture_handle)
                    return Bound::failure(texture_handle.error());
                material.data.texture_properties[binding.property] = texture_handle.value();
                if(textures.insert(texture_handle.value()).second)
                    result.textures.push_back(
                        {texture_handle.value(), texture.image, binding.settings, texture.sampler});
            }
            material_handles.push_back(handle.value());
            result.materials.push_back({handle.value(), std::move(material.data)});
        }
        AssetHandle default_material;
        std::vector<ModelData::Part> parts;
        for(auto& mesh : imported.meshes) {
            std::string key = "mesh/";
            if(!mesh.name.empty() && mesh_name_counts[mesh.name] == 1)
                key += "name/" + std::to_string(mesh.name.size()) + ":" + mesh.name;
            else
                key += "index/" + std::to_string(mesh.source_mesh);
            key += "/primitive/" + std::to_string(mesh.source_primitive);
            if(mesh.skin_node) {
                if(*mesh.skin_node >= node_keys.size())
                    return Bound::failure("Model mesh references an invalid skin node");
                key += "/skin/" + node_keys[*mesh.skin_node];
            }
            const auto handle = assign(key, AssetType::Mesh);
            if(!handle)
                return Bound::failure(handle.error());
            AssetHandle material;
            if(mesh.material != ModelImportData::NONE) {
                if(mesh.material >= material_handles.size())
                    return Bound::failure("Model mesh references an invalid material");
                material = material_handles[mesh.material];
            } else {
                if(!default_material) {
                    const auto handle = assign("material/default", AssetType::Material);
                    if(!handle)
                        return Bound::failure(handle.error());
                    default_material = handle.value();
                    // glTF 缺省 metallic 为 1，与引擎编辑器新建材质的缺省值独立。
                    result.materials.push_back({default_material,
                        MaterialData{.template_name = "pbr",
                            .scalar_properties = {{"metallic", 1.f}, {"roughness", 1.f}}}});
                }
                material = default_material;
            }
            parts.push_back({handle.value(), material});
            result.meshes.push_back({handle.value(), std::move(mesh.data)});
        }
        for(size_t i = 0; i < imported.meshes.size(); ++i) {
            const auto& mesh = imported.meshes[i];
            for(size_t level = 0; level < mesh.lods.size(); ++level) {
                const auto selected = mesh.lods[level];
                if(selected == ModelImportData::NONE)
                    continue;
                if(selected >= parts.size() || selected == i)
                    return Bound::failure("Model references an invalid LOD mesh");
                if(level == 0)
                    parts[i].lod.level1 = parts[selected].mesh;
                else
                    parts[i].lod.level2 = parts[selected].mesh;
            }
            parts[i].collision = mesh.collision;
        }
        for(size_t index = 0; index < imported.nodes.size(); ++index) {
            auto& node = imported.nodes[index];
            ModelData::Node output{.key = "node/" + node_keys[index],
                .name = std::move(node.name),
                .parent = node.parent,
                .transform = node.transform,
                .wind = node.wind,
                .wind_height = node.wind_height};
            for(const auto mesh : node.meshes) {
                if(mesh >= parts.size())
                    return Bound::failure("Model node references an invalid mesh");
                output.parts.push_back(parts[mesh]);
            }
            result.model.nodes.push_back(std::move(output));
        }
        for(auto& scene : imported.scenes)
            result.model.scenes.push_back({std::move(scene.name), std::move(scene.roots)});
        result.images = std::move(imported.images);
        result.source_dependencies = std::move(imported.source_dependencies);
        std::ranges::sort(result.metadata.subresources, {}, &AssetSubresource::key);
        return Bound::success(std::move(result));
    }
}
