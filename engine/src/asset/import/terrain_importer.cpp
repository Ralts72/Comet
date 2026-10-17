#include "asset/import/terrain_importer.h"
#include "asset/data/terrain_data.h"
#include "asset/data/texture_sampling.h"
#include "asset/import/image_encoding.h"
#include "asset/import/texture_importer.h"
#include "common/file_io.h"

#include <stb_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstring>
#include <memory>
#include <random>

namespace Comet {
    namespace {
        Result<TerrainData> read_source(
            const std::filesystem::path& path, const AssetImportLimits& limits) {
            auto bytes =
                read_binary_file(path, std::min(limits.source_bytes, size_t(16 * 1024 * 1024)));
            if(!bytes)
                return Result<TerrainData>::failure(bytes.error());
            return TerrainData::deserialize(
                {reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()},
                path.string());
        }
        std::filesystem::path dependency(
            const std::filesystem::path& source, const std::string& value) {
            return (source.parent_path() / value).lexically_normal();
        }
        float smooth(float first, float last, float value) {
            const float t = std::clamp((value - first) / (last - first), 0.f, 1.f);
            return t * t * (3 - 2 * t);
        }
        struct HeightField {
            TerrainData data;
            float height(float x, float z) const {
                x = std::clamp(x, 0.f, float(data.resolution - 1));
                z = std::clamp(z, 0.f, float(data.resolution - 1));
                const uint32_t x0 = uint32_t(x), z0 = uint32_t(z);
                const auto at = [&](uint32_t a, uint32_t b) {
                    return data.heights[size_t(b) * data.resolution + a];
                };
                const uint32_t x1 = std::min(x0 + 1, data.resolution - 1),
                               z1 = std::min(z0 + 1, data.resolution - 1);
                return std::lerp(std::lerp(at(x0, z0), at(x1, z0), x - x0),
                    std::lerp(at(x0, z1), at(x1, z1), x - x0), z - z0);
            }
            Math::Vec3 normal(float x, float z) const {
                const float left = std::max(x - 1, 0.f),
                            right = std::min(x + 1, float(data.resolution - 1));
                const float near = std::max(z - 1, 0.f),
                            far = std::min(z + 1, float(data.resolution - 1));
                const float dx = (height(right, z) - height(left, z)) * data.height_scale
                                 / ((right - left) * data.width / (data.resolution - 1));
                const float dz = (height(x, far) - height(x, near)) * data.height_scale
                                 / ((far - near) * data.depth / (data.resolution - 1));
                return glm::normalize(Math::Vec3(-dx, 1, -dz));
            }
        };
        Result<HeightField> load_heights(TerrainData config, const std::filesystem::path& source,
            const AssetImportLimits& limits) {
            HeightField field{std::move(config)};
            auto& data = field.data;
            if(data.heightmap.empty()) {
                if(data.heights.empty())
                    data.heights.resize(size_t(data.resolution) * data.resolution);
                return Result<HeightField>::success(std::move(field));
            }
            auto bytes = read_binary_file(
                dependency(source, data.heightmap), std::min(limits.source_bytes, size_t(INT_MAX)));
            if(!bytes)
                return Result<HeightField>::failure(bytes.error());
            const auto* encoded = reinterpret_cast<const stbi_uc*>(bytes.value().data());
            const int length = int(bytes.value().size());
            int width = 0, height = 0, channels = 0;
            if(!stbi_info_from_memory(encoded, length, &width, &height, &channels)
                || width != int(data.resolution) || height != width)
                return Result<HeightField>::failure(
                    "高度图尺寸必须与地形分辨率相同（正方形 2^n+1）");
            std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
                stbi_load_16_from_memory(encoded, length, &width, &height, &channels, 1),
                stbi_image_free);
            if(!pixels)
                return Result<HeightField>::failure("无法解码地形高度图");
            data.heights.resize(size_t(width) * height);
            for(size_t i = 0; i < data.heights.size(); ++i)
                data.heights[i] = pixels.get()[i] / 65535.f;
            return Result<HeightField>::success(std::move(field));
        }
        MeshData chunk_mesh(
            const HeightField& field, uint32_t start_x, uint32_t start_z, uint32_t stride) {
            const auto& data = field.data;
            MeshData mesh;
            const uint32_t cells = data.chunk_cells / stride, edge = cells + 1;
            mesh.vertices.reserve(size_t(edge) * edge + 4 * cells);
            mesh.indices.reserve(size_t(cells) * cells * 6 + cells * 24);
            for(uint32_t z = 0; z <= cells; ++z)
                for(uint32_t x = 0; x <= cells; ++x) {
                    const float gx = float(start_x + x * stride), gz = float(start_z + z * stride);
                    const auto normal = field.normal(gx, gz);
                    const auto tangent = glm::normalize(Math::Vec3(normal.y, -normal.x, 0));
                    mesh.vertices.push_back({{x * stride * data.width / (data.resolution - 1),
                                                 field.height(gx, gz) * data.height_scale,
                                                 z * stride * data.depth / (data.resolution - 1)},
                        {gx / (data.resolution - 1), gz / (data.resolution - 1)}, normal,
                        Math::Vec4(tangent, -1)});
                }
            for(uint32_t z = 0; z < cells; ++z)
                for(uint32_t x = 0; x < cells; ++x) {
                    const uint32_t a = z * edge + x, b = a + 1, c = a + edge, d = c + 1;
                    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
                }
            // 边界重复顶点向下延伸，覆盖相邻分块选择不同 LOD 时的裂缝。
            std::vector<uint32_t> boundary;
            boundary.reserve(cells * 4);
            for(uint32_t x = 0; x < cells; ++x)
                boundary.push_back(x);
            for(uint32_t z = 0; z < cells; ++z)
                boundary.push_back(z * edge + cells);
            for(uint32_t x = cells; x > 0; --x)
                boundary.push_back(cells * edge + x);
            for(uint32_t z = cells; z > 0; --z)
                boundary.push_back(z * edge);
            const uint32_t skirt = uint32_t(mesh.vertices.size());
            for(auto index : boundary) {
                auto vertex = mesh.vertices[index];
                vertex.position.y -= data.skirt_depth;
                mesh.vertices.push_back(vertex);
            }
            for(uint32_t i = 0; i < boundary.size(); ++i) {
                const uint32_t next = (i + 1) % uint32_t(boundary.size());
                mesh.indices.insert(
                    mesh.indices.end(), {boundary[i], boundary[next], skirt + i, boundary[next],
                                            skirt + next, skirt + i});
            }
            return mesh;
        }
        uint8_t srgb(float linear) {
            const float value = linear <= .0031308f ? linear * 12.92f
                                                    : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f;
            return uint8_t(std::lround(std::clamp(value, 0.f, 1.f) * 255));
        }
        Result<void> add_texture(ModelImportData& model, std::string name, TextureData pixels) {
            auto png = encode_png_rgba(pixels);
            if(!png)
                return Result<void>::failure(png.error());
            std::vector<std::byte> encoded(png.value().size());
            std::memcpy(encoded.data(), png.value().data(), encoded.size());
            const uint32_t image = uint32_t(model.images.size());
            model.images.push_back({name, std::move(encoded)});
            SamplerDesc sampler;
            sampler.address_mode_u = sampler.address_mode_v = SamplerAddressMode::ClampToEdge;
            model.textures.push_back({std::move(name), image, sampler, true});
            return Result<void>::success();
        }
        Result<void> bake_material(ModelImportData& model, const HeightField& field,
            const std::filesystem::path& source, const AssetImportLimits& limits, size_t budget) {
            const auto& data = field.data;
            struct LayerImages {
                std::optional<CpuTextureImage> albedo, normal, roughness;
            };
            std::array<LayerImages, 3> textures;
            size_t remaining = budget;
            for(size_t layer = 0; layer < data.layers.size(); ++layer) {
                const auto& config = data.layers[layer];
                const std::array paths{
                    config.texture, config.normal_texture, config.roughness_texture};
                const std::array targets{
                    &textures[layer].albedo, &textures[layer].normal, &textures[layer].roughness};
                for(size_t kind = 0; kind < paths.size(); ++kind) {
                    if(paths[kind].empty())
                        continue;
                    TextureImportSettings settings;
                    settings.color_space =
                        kind == 0 ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
                    settings.normal_map = kind == 1;
                    auto decoded = TextureImporter{}.import(
                        dependency(source, paths[kind]), settings, remaining, limits);
                    if(!decoded)
                        return Result<void>::failure(decoded.error());
                    auto image = CpuTextureImage::decode(decoded.value(), remaining);
                    if(!image)
                        return Result<void>::failure(image.error());
                    remaining -= image.value().pixels.size() * sizeof(Math::Vec4);
                    *targets[kind] = std::move(image).value();
                }
            }
            TextureData image{
                .width = int(data.albedo_resolution), .height = int(data.albedo_resolution)};
            image.pixels.resize(size_t(image.width) * image.height * 4);
            auto normals = image, roughness = image;
            for(int y = 0; y < image.height; ++y)
                for(int x = 0; x < image.width; ++x) {
                    const Math::Vec2 uv(
                        float(x) / (image.width - 1), float(y) / (image.height - 1));
                    const auto grid = uv * float(data.resolution - 1);
                    const float rock = smooth(
                        data.slope_start, data.slope_end, 1 - field.normal(grid.x, grid.y).y);
                    const float high =
                        smooth(data.height_start, data.height_end, field.height(grid.x, grid.y));
                    const std::array weights{(1 - rock) * (1 - high), rock, (1 - rock) * high};
                    Math::Vec3 color(0), normal(0);
                    float rough = 0;
                    for(size_t i = 0; i < textures.size(); ++i) {
                        const auto& config = data.layers[i];
                        const auto tiled = uv * config.tiling;
                        auto layer = config.color;
                        if(textures[i].albedo)
                            layer *= Math::Vec3(textures[i].albedo->sample(tiled));
                        color += layer * weights[i];
                        Math::Vec3 direction(0, 0, 1);
                        if(textures[i].normal) {
                            direction = Math::Vec3(textures[i].normal->sample(tiled)) * 2.f - 1.f;
                            direction.x *= config.normal_scale;
                            direction.y *= config.normal_scale;
                            direction.z = std::max(direction.z, .001f);
                            direction = glm::normalize(direction);
                        }
                        normal += direction * weights[i];
                        rough +=
                            config.roughness
                            * (textures[i].roughness ? textures[i].roughness->sample(tiled).r : 1)
                            * weights[i];
                    }
                    normal = glm::normalize(normal);
                    const auto offset = (size_t(y) * image.width + x) * 4;
                    for(int c = 0; c < 3; ++c) {
                        image.pixels[offset + c] = srgb(color[c]);
                        normals.pixels[offset + c] =
                            uint8_t(std::lround(std::clamp(normal[c] * .5f + .5f, 0.f, 1.f) * 255));
                    }
                    image.pixels[offset + 3] = normals.pixels[offset + 3] = 255;
                    roughness.pixels[offset] = roughness.pixels[offset + 3] = 255;
                    roughness.pixels[offset + 1] =
                        uint8_t(std::lround(std::clamp(rough, 0.f, 1.f) * 255));
                    roughness.pixels[offset + 2] = 0;
                }
            if(auto result = add_texture(model, "terrain/albedo", std::move(image)); !result)
                return result;
            if(auto r = add_texture(model, "terrain/normal", std::move(normals)); !r)
                return r;
            if(auto r = add_texture(model, "terrain/roughness", std::move(roughness)); !r)
                return r;
            ModelImportData::Material material;
            material.name = "terrain/surface";
            material.data.template_name = "pbr";
            material.data.scalar_properties = {{"metallic", 0}, {"roughness", data.roughness}};
            material.textures.push_back({"base_color_texture", 0, {}});
            material.textures.push_back({"normal_texture", 1,
                {.color_space = TextureColorSpace::Linear, .normal_map = true}});
            material.textures.push_back(
                {"metallic_roughness_texture", 2, {.color_space = TextureColorSpace::Linear}});
            model.materials.push_back(std::move(material));
            return Result<void>::success();
        }
        using VegetationMeshes = std::vector<uint32_t>;
        Result<VegetationMeshes> import_vegetation_meshes(ModelImportData& model,
            const std::filesystem::path& source, const AssetImportLimits& limits, size_t budget) {
            using Prepared = Result<VegetationMeshes>;
            VegetationMeshes grass_meshes;
            auto imported = ModelImporter{}.import(source, budget, limits);
            if(!imported)
                return Prepared::failure(imported.error());
            auto& custom = imported.value();
            const auto material_base = uint32_t(model.materials.size());
            const auto texture_base = uint32_t(model.textures.size());
            const auto image_base = uint32_t(model.images.size());
            for(size_t i = 0; i < custom.materials.size(); ++i) {
                auto& material = custom.materials[i];
                material.name = i == 0 ? "vegetation/surface" : "vegetation/" + material.name;
                for(auto& texture : material.textures)
                    texture.texture += texture_base;
                model.materials.push_back(std::move(material));
            }
            for(auto& texture : custom.textures) {
                texture.name = "vegetation/" + texture.name;
                texture.image += image_base;
                model.textures.push_back(std::move(texture));
            }
            for(auto& image : custom.images) {
                image.name = "vegetation/" + image.name;
                model.images.push_back(std::move(image));
            }
            std::vector<Math::Mat4> transforms(custom.nodes.size(), Math::Mat4(1));
            std::vector<std::vector<uint32_t>> children(custom.nodes.size());
            for(uint32_t i = 0; i < custom.nodes.size(); ++i)
                if(custom.nodes[i].parent != ModelImportData::NONE)
                    children[custom.nodes[i].parent].push_back(i);
            auto order = custom.scenes[custom.default_scene].roots;
            for(size_t cursor = 0; cursor < order.size(); ++cursor) {
                const auto index = order[cursor];
                const auto& node = custom.nodes[index];
                const auto& t = node.transform;
                auto world = Math::translate(Math::Mat4(1), t.translation)
                             * glm::mat4_cast(t.rotation) * Math::scale(Math::Mat4(1), t.scale);
                if(node.parent != ModelImportData::NONE)
                    world = transforms[node.parent] * world;
                transforms[index] = world;
                const auto basis = Math::Mat3(world);
                const auto determinant = glm::determinant(basis);
                if(std::abs(determinant) < 1e-8f)
                    return Prepared::failure("植被模型变换不可为零缩放");
                const auto normals = glm::transpose(glm::inverse(basis));
                for(const auto selected : node.meshes) {
                    const auto& original = custom.meshes[selected];
                    if(original.skin_node)
                        return Prepared::failure("植被散布需要静态网格");
                    auto mesh = original;
                    mesh.name = grass_meshes.empty()
                                    ? "vegetation/blade"
                                    : "vegetation/" + std::to_string(grass_meshes.size());
                    mesh.source_mesh = uint32_t(model.meshes.size());
                    mesh.source_primitive = 0;
                    if(mesh.material != ModelImportData::NONE)
                        mesh.material += material_base;
                    for(auto& vertex : mesh.data.vertices) {
                        vertex.position = Math::Vec3(world * Math::Vec4(vertex.position, 1));
                        vertex.normal = glm::normalize(normals * vertex.normal);
                        auto tangent = basis * Math::Vec3(vertex.tangent);
                        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                        if(glm::length(tangent) > 1e-8f)
                            tangent = glm::normalize(tangent);
                        vertex.tangent =
                            Math::Vec4(tangent, vertex.tangent.w * (determinant < 0 ? -1.f : 1.f));
                    }
                    if(determinant < 0)
                        for(size_t i = 0; i < mesh.data.indices.size(); i += 3)
                            std::swap(mesh.data.indices[i], mesh.data.indices[i + 1]);
                    grass_meshes.push_back(uint32_t(model.meshes.size()));
                    model.meshes.push_back(std::move(mesh));
                }
                order.insert(order.end(), children[index].begin(), children[index].end());
            }
            if(grass_meshes.empty())
                return Prepared::failure("植被模型没有可绘制网格");
            return Prepared::success(std::move(grass_meshes));
        }
        Result<VegetationMeshes> make_default_vegetation(ModelImportData& model) {
            using Prepared = Result<VegetationMeshes>;
            TextureData mask{.width = 32, .height = 64};
            mask.pixels.resize(32 * 64 * 4);
            for(int y = 0; y < 64; ++y)
                for(int x = 0; x < 32; ++x) {
                    const float v = y / 63.f, u = (x / 31.f - .5f) * 2;
                    const bool leaf = std::abs(u - .2f * std::sin(v * 6)) < .18f + .42f * v;
                    const size_t p = (size_t(y) * 32 + x) * 4;
                    mask.pixels[p] = 75;
                    mask.pixels[p + 1] = uint8_t(130 + 60 * v);
                    mask.pixels[p + 2] = 35;
                    mask.pixels[p + 3] = leaf ? 255 : 0;
                }
            if(auto result = add_texture(model, "vegetation/blade", std::move(mask)); !result)
                return Prepared::failure(result.error());
            ModelImportData::Material material;
            material.name = "vegetation/surface";
            material.data.template_name = "pbr";
            material.data.surface = {AlphaMode::Mask, .5f, true};
            material.data.scalar_properties = {{"metallic", 0}, {"roughness", .9f}};
            material.textures.push_back(
                {"base_color_texture", uint32_t(model.textures.size() - 1), {}});
            const auto material_index = uint32_t(model.materials.size());
            model.materials.push_back(std::move(material));
            MeshData mesh;
            for(int axis = 0; axis < 2; ++axis) {
                const auto position = [axis](float x, float y) {
                    return axis ? Math::Vec3(0, y, x) : Math::Vec3(x, y, 0);
                };
                const auto normal = axis ? Math::Vec3(-1, 0, 0) : Math::Vec3(0, 0, 1);
                const auto tangent = axis ? Math::Vec3(0, 0, 1) : Math::Vec3(1, 0, 0);
                for(const auto& uv :
                    {Math::Vec2(0, 1), Math::Vec2(1, 1), Math::Vec2(0, 0), Math::Vec2(1, 0)})
                    mesh.vertices.push_back({position((uv.x - .5f) * .7f, 1 - uv.y), uv, normal,
                        Math::Vec4(tangent, -1)});
                const uint32_t base = axis * 4;
                mesh.indices.insert(
                    mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
            }
            const uint32_t grass_mesh = uint32_t(model.meshes.size());
            model.meshes.push_back(
                {"vegetation/blade", grass_mesh, 0, {}, material_index, std::move(mesh)});
            return Prepared::success({grass_mesh});
        }
        Result<void> add_vegetation(ModelImportData& model, const HeightField& field,
            const std::filesystem::path& source, const AssetImportLimits& limits, size_t budget) {
            const auto& data = field.data;
            const auto& config = data.vegetation;
            if(!config.count)
                return Result<void>::success();
            auto meshes = config.model.empty()
                              ? make_default_vegetation(model)
                              : import_vegetation_meshes(
                                    model, dependency(source, config.model), limits, budget);
            if(!meshes)
                return Result<void>::failure(meshes.error());
            if(config.count * meshes.value().size() * sizeof(uint32_t) > budget)
                return Result<void>::failure("植被实例超过内存预算");
            std::mt19937 random(config.seed);
            // 使用确定的整数转换，不依赖不同标准库的 uniform_real_distribution 实现。
            const auto unit = [&] { return float(random() >> 8) / 16777216.f; };
            const float min_normal = std::cos(glm::radians(config.max_slope));
            uint32_t placed = 0;
            for(uint32_t attempt = 0; attempt < config.count * 20 && placed < config.count;
                ++attempt) {
                const float u = unit(), v = unit();
                const float x = u * (data.resolution - 1), z = v * (data.resolution - 1),
                            h = field.height(x, z);
                if(h < config.min_elevation || h > config.max_elevation
                    || field.normal(x, z).y < min_normal)
                    continue;
                ModelImportData::Node node;
                node.name = "vegetation/" + std::to_string(placed++);
                node.transform.translation = {
                    (u - .5f) * data.width, h * data.height_scale - .04f, (v - .5f) * data.depth};
                node.transform.rotation =
                    glm::angleAxis(unit() * glm::two_pi<float>(), Math::Vec3(0, 1, 0));
                node.transform.scale =
                    Math::Vec3(std::lerp(config.min_height, config.max_height, unit()));
                node.meshes = meshes.value();
                node.wind = {
                    config.wind_amplitude, 0, config.wind_amplitude * .6f, config.wind_frequency};
                model.scenes[0].roots.push_back(uint32_t(model.nodes.size()));
                model.nodes.push_back(std::move(node));
            }
            return Result<void>::success();
        }

        Result<ModelImporter::SourceInfo> inspect_terrain(const TerrainData& data,
            const std::filesystem::path& source, const AssetImportLimits& limits) {
            using Info = Result<ModelImporter::SourceInfo>;
            const size_t chunks = (data.resolution - 1) / data.chunk_cells;
            // 包含候选、PNG 临时缓冲、所有高度及最坏的节点字符串开销；纹理解码另行预留。
            size_t bytes =
                16 * 1024 * 1024 + size_t(data.resolution) * data.resolution * sizeof(float)
                + chunks * chunks
                      * (size_t(data.chunk_cells + 1) * (data.chunk_cells + 1)
                          + 4 * data.chunk_cells)
                      * sizeof(MeshVertex) * 2
                + chunks * chunks
                      * (size_t(data.chunk_cells) * data.chunk_cells * 6 + data.chunk_cells * 24)
                      * sizeof(uint32_t) * 2
                + size_t(data.albedo_resolution) * data.albedo_resolution * 48
                + data.vegetation.count * 512;
            ModelImporter::SourceInfo info{bytes, {}};
            if(!data.heightmap.empty()) {
                const auto path = dependency(source, data.heightmap);
                std::error_code error;
                const auto bytes = std::filesystem::file_size(path, error);
                if(error)
                    return Info::failure("Cannot inspect heightmap: " + error.message());
                if(bytes > std::min(limits.source_bytes, size_t(INT_MAX)))
                    return Info::failure("Heightmap exceeds source byte budget");
                info.working_bytes += size_t(bytes) + size_t(data.resolution) * data.resolution * 2;
                info.dependencies.push_back(path);
            }
            for(const auto& layer : data.layers)
                for(const auto& texture :
                    {layer.texture, layer.normal_texture, layer.roughness_texture}) {
                    if(texture.empty())
                        continue;
                    const auto path = dependency(source, texture);
                    auto working = TextureImporter::working_bytes(path, limits);
                    if(!working)
                        return Info::failure(working.error());
                    if(working.value()
                        > (limits.mesh_working_bytes
                              - std::min(info.working_bytes, limits.mesh_working_bytes))
                              / 3)
                        return Info::failure("地形贴图超过导入内存预算");
                    info.working_bytes += working.value() * 3;
                    info.dependencies.push_back(path);
                }
            if(!data.vegetation.model.empty() && data.vegetation.count) {
                const auto path = dependency(source, data.vegetation.model);
                auto vegetation = ModelImporter::inspect_source(path, limits);
                if(!vegetation)
                    return Info::failure(vegetation.error());
                if(vegetation.value().working_bytes
                    > limits.mesh_working_bytes
                          - std::min(info.working_bytes, limits.mesh_working_bytes))
                    return Info::failure("植被模型超过导入内存预算");
                info.working_bytes += vegetation.value().working_bytes;
                info.dependencies.push_back(path);
                for(const auto& item : vegetation.value().dependencies)
                    info.dependencies.push_back(item);
            }
            if(info.working_bytes > limits.mesh_working_bytes)
                return Info::failure("地形超过导入内存预算");
            std::sort(info.dependencies.begin(), info.dependencies.end());
            info.dependencies.erase(std::unique(info.dependencies.begin(), info.dependencies.end()),
                info.dependencies.end());
            return Info::success(std::move(info));
        }
    }
    Result<ModelImporter::SourceInfo> TerrainImporter::inspect_source(
        const std::filesystem::path& source, const AssetImportLimits& limits) {
        auto data = read_source(source, limits);
        if(!data)
            return Result<ModelImporter::SourceInfo>::failure(data.error());
        return inspect_terrain(data.value(), source, limits);
    }
    Result<ModelImportData> TerrainImporter::import(const std::filesystem::path& source,
        size_t memory_budget, const AssetImportLimits& limits) const {
        using Imported = Result<ModelImportData>;
        auto data = read_source(source, limits);
        if(!data)
            return Imported::failure(data.error());
        auto info = inspect_terrain(data.value(), source, limits);
        if(!info)
            return Imported::failure(info.error());
        if(info.value().working_bytes > memory_budget)
            return Imported::failure("地形超过本次导入内存预算");
        auto heights = load_heights(std::move(data).value(), source, limits);
        if(!heights)
            return Imported::failure(heights.error());
        const auto& field = heights.value();
        const auto& config = field.data;
        ModelImportData model;
        model.source_dependencies = info.value().dependencies;
        model.scenes.push_back({"Terrain", {}});
        const size_t chunks = (config.resolution - 1) / config.chunk_cells;
        model.meshes.reserve(chunks * chunks * 3 + (config.vegetation.count ? 1 : 0));
        model.nodes.reserve(chunks * chunks + config.vegetation.count);
        model.scenes[0].roots.reserve(chunks * chunks + config.vegetation.count);
        for(uint32_t z = 0; z < config.resolution - 1; z += config.chunk_cells)
            for(uint32_t x = 0; x < config.resolution - 1; x += config.chunk_cells) {
                const uint32_t base = uint32_t(model.meshes.size()), source_mesh = base / 3;
                const auto name = "terrain/" + std::to_string(x) + "/" + std::to_string(z);
                for(uint32_t lod = 0; lod < 3; ++lod) {
                    model.meshes.push_back(
                        {name, source_mesh, lod, {}, 0, chunk_mesh(field, x, z, 1u << lod)});
                    if(lod == 0) {
                        model.meshes.back().lods = {base + 1, base + 2};
                        model.meshes.back().collision = config.collision;
                    }
                }
                ModelImportData::Node node;
                node.name = name;
                node.transform.translation = {
                    -config.width * .5f + x * config.width / (config.resolution - 1), 0,
                    -config.depth * .5f + z * config.depth / (config.resolution - 1)};
                node.meshes = {base};
                model.scenes[0].roots.push_back(uint32_t(model.nodes.size()));
                model.nodes.push_back(std::move(node));
            }
        if(auto result = bake_material(model, field, source, limits, memory_budget); !result)
            return Imported::failure(result.error());
        if(auto result = add_vegetation(model, field, source, limits, memory_budget); !result)
            return Imported::failure(result.error());
        return Imported::success(std::move(model));
    }
}
