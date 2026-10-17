#include "render/ray_scene.h"

#include "common/binary.h"
#include "asset/data/texture_sampling.h"
#include "asset/data/cubemap_sampling.h"
#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/material/material_runtime.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <utility>

namespace Comet {
    namespace {
        void write(Binary::Writer& key, Math::Vec3 value) {
            for(int i = 0; i < 3; ++i)
                key.write_float(value[i]);
        }

        void write_material(Binary::Writer& key, const MaterialBinding& binding,
            const RaySceneBuilder::Images& images) {
            key.write_u64(binding.material_handle.value());
            key.write_u64(binding.asset_revision);
            key.write_u64(binding.resource ? binding.resource->get_revision() : 0);
            if(binding.resource) {
                const auto& material = *binding.resource;
                if(const auto layout = MaterialLayout::find_builtin(material.get_template_name()))
                    for(const auto& texture : layout->get_textures()) {
                        const auto handle = material.get_texture_source(texture.name);
                        key.write_u64(handle.value());
                        key.write_u64(images.revision && handle ? images.revision(handle) : 0);
                    }
            }
            key.write_u32(bool(binding.overrides));
            if(binding.overrides) {
                const auto& overrides = *binding.overrides;
                key.write_u64(overrides.instance_id);
                key.write_u64(overrides.material.value());
                key.write_u64(overrides.scalar_properties.size());
                for(const auto& [name, value] : overrides.scalar_properties) {
                    key.write_string(name, name.size());
                    key.write_float(value);
                }
                key.write_u64(overrides.vector_properties.size());
                for(const auto& [name, value] : overrides.vector_properties) {
                    key.write_string(name, name.size());
                    for(int axis = 0; axis < 4; ++axis)
                        key.write_float(value[axis]);
                }
            }
        }

        std::vector<std::byte> lighting_key(const RenderWorld& world) {
            Binary::Writer key;
            key.write_u64(world.lights.size());
            for(const auto& light : world.lights) {
                key.write_u32(static_cast<uint32_t>(light.type));
                write(key, light.position);
                write(key, light.direction);
                write(key, light.color);
                for(float value :
                    {light.intensity, light.range, light.inner_angle, light.outer_angle})
                    key.write_float(value);
            }
            return key.data();
        }

        std::vector<std::byte> settings_key(const SceneEnvironment& environment) {
            Binary::Writer key;
            write(key, environment.background_color);
            key.write_float(environment.intensity);
            key.write_float(environment.lighting_intensity);
            key.write_float(environment.rotation);
            key.write_u32(environment.background);
            key.write_u32(environment.lighting);
            return key.data();
        }

        bool update_key(std::vector<std::byte>& cached, std::vector<std::byte> key) {
            if(cached == key)
                return false;
            cached = std::move(key);
            return true;
        }

        struct GeometryKeys {
            std::vector<std::byte> topology;
            std::vector<std::byte> transforms;
        };
        GeometryKeys geometry_keys(
            const RenderWorld& world, const RaySceneBuilder::MeshRevision& revision) {
            Binary::Writer topology, transforms;
            topology.write_u64(world.scene_lifetime);
            topology.write_u64(world.render_items.size());
            for(const auto& item : world.render_items) {
                topology.write_u64(item.mesh_handle.value());
                topology.write_u64(item.mesh_revision);
                topology.write_u64(revision ? revision(item.mesh_handle) : 0);
                topology.write_u32(bool(item.mesh));
                for(int column = 0; column < 4; ++column)
                    for(int row = 0; row < 4; ++row)
                        transforms.write_float(item.model_matrix[column][row]);
            }
            return {topology.data(), transforms.data()};
        }

        std::vector<std::byte> material_key(
            const RenderWorld& world, const RaySceneBuilder::Images& images) {
            Binary::Writer key;
            key.write_u64(world.scene_lifetime);
            key.write_u64(world.render_items.size());
            for(const auto& item : world.render_items)
                write_material(key, item.material, images);
            return key.data();
        }

        std::vector<std::byte> environment_key(
            const RenderWorld& world, const RaySceneBuilder::Images& images) {
            Binary::Writer key;
            const auto handle = world.environment.asset;
            key.write_u64(handle.value());
            key.write_u64(world.environment_revision);
            key.write_u64(images.revision && handle ? images.revision(handle) : 0);
            key.write_u32(world.environment.background || world.environment.lighting);
            return key.data();
        }

        class TexturePacker {
        public:
            TexturePacker(RayScene::Materials& scene, const RaySceneBuilder::Images& sources,
                uint32_t maximum_dimension)
                : m_scene(scene), m_sources(sources), m_maximum_dimension(maximum_dimension) {
                m_scene.textures.emplace_back();
            }

            Result<uint32_t> add(const Material& material, const std::string& property) {
                using Added = Result<uint32_t>;
                if(!material.get_texture_property(property))
                    return Added::success(0);
                const auto handle = material.get_texture_source(property);
                if(!handle || !m_sources.texture)
                    return Added::failure("射线场景贴图没有 CPU 源：" + property);
                if(const auto found = m_textures.find(handle); found != m_textures.end())
                    return Added::success(found->second);
                auto data = m_sources.texture(handle);
                if(!data)
                    return Added::failure(data.error().message);
                auto decoded =
                    CpuTextureImage::decode(data.value(), remaining(), m_maximum_dimension);
                if(!decoded)
                    return Added::failure(
                        decoded.error() + ": " + property + " [" + std::to_string(handle.value())
                        + "] " + std::to_string(data.value().width) + "x"
                        + std::to_string(data.value().height)
                        + ", payload=" + std::to_string(data.value().pixels.size())
                        + ", remaining=" + std::to_string(remaining()));
                const auto& image = decoded.value();
                const auto index = static_cast<uint32_t>(m_scene.textures.size());
                m_scene.textures.push_back({{static_cast<uint32_t>(m_scene.texels.size()),
                                                image.width, image.height, 0},
                    {uint32_t(image.sampler.address_mode_u), uint32_t(image.sampler.address_mode_v),
                        uint32_t(image.sampler.mag_filter), 0}});
                m_scene.texels.insert(
                    m_scene.texels.end(), image.pixels.begin(), image.pixels.end());
                m_textures.emplace(handle, index);
                return Added::success(index);
            }

        private:
            size_t remaining() const {
                return RayScene::MAX_TEXTURE_BYTES - m_scene.texels.size() * sizeof(Math::Vec4);
            }
            RayScene::Materials& m_scene;
            const RaySceneBuilder::Images& m_sources;
            std::map<AssetHandle, uint32_t> m_textures;
            uint32_t m_maximum_dimension;
        };

        Result<std::shared_ptr<const RayScene::Environment>> build_environment(
            const SceneEnvironment& settings, const RaySceneBuilder::Images& sources) {
            using Built = Result<std::shared_ptr<const RayScene::Environment>>;
            auto output = std::make_shared<RayScene::Environment>();
            if(!settings.asset || (!settings.background && !settings.lighting))
                return Built::success(output);
            if(!sources.environment)
                return Built::failure("射线场景没有 CPU 环境读取服务");
            auto data = sources.environment(settings.asset);
            if(!data)
                return Built::failure(data.error().message);
            auto valid = CpuCubeSampler::validate(data.value());
            if(!valid)
                return Built::failure(valid.error());
            if(valid.value() > RayScene::MAX_TEXTURE_BYTES)
                return Built::failure("射线场景环境超过 CPU 解码预算");
            CpuCubeSampler cube(data.value());
            const uint32_t width = std::clamp(uint32_t(data.value().width) * 4, 16u, 1024u);
            const uint32_t height = width / 2;
            const auto count = size_t(width) * height;
            if(count * sizeof(Math::Vec4) > RayScene::MAX_TEXTURE_BYTES)
                return Built::failure("射线场景环境超过纹理预算");
            output->size = {width, height};
            output->texels.resize(count);
            double sum = 0;
            for(uint32_t y = 0; y < height; ++y) {
                const float vertical = 1.f - 2.f * (y + .5f) / height;
                const float radius = std::sqrt(std::max(0.f, 1 - vertical * vertical));
                for(uint32_t x = 0; x < width; ++x) {
                    const float angle = 2 * Math::PI * ((x + .5f) / width - .5f);
                    const auto color =
                        cube.sample({radius * std::cos(angle), vertical, radius * std::sin(angle)});
                    auto& pixel = output->texels[size_t(y) * width + x];
                    pixel = {color, glm::dot(color, Math::Vec3(.2126f, .7152f, .0722f))};
                    if(!Math::is_finite(pixel))
                        return Built::failure("射线场景环境解码溢出");
                    sum += pixel.w;
                }
            }
            // 等面积参数化下像素的立体角相同；GPU 使用实际 float CDF 差值求 PDF。
            double cumulative = 0;
            for(size_t i = 0; i < count; ++i) {
                auto& pixel = output->texels[i];
                cumulative += sum > 0 ? double(pixel.w) / sum : 1.0 / count;
                pixel.w = static_cast<float>(cumulative);
            }
            output->texels.back().w = 1;
            return Built::success(output);
        }

        Result<RayScene::Surface> surface(
            const MaterialBinding& binding, MaterialRuntimeCache& cache, TexturePacker& textures) {
            using Surface = Result<RayScene::Surface>;
            if(!binding.resource)
                return Surface::failure("射线场景材质尚未加载");
            const auto& material = *binding.resource;
            const auto layout = MaterialLayout::find_builtin(material.get_template_name());
            if(material.get_shader_program() || !layout
                || (material.get_template_name() != "pbr"
                    && material.get_template_name() != "unlit_color"))
                return Surface::failure("射线场景仅支持内置 PBR／无光照材质：" + material.get_name()
                                        + " [" + std::to_string(binding.material_handle.value())
                                        + "]");
            auto prepared =
                cache.prepare(binding.material_handle, binding.resource, layout, binding.overrides);
            if(!prepared)
                return Surface::failure(prepared.error());
            const auto scalar = [&](const std::string& name) {
                return prepared.value()->get_scalar_property(name).value();
            };
            const auto vector = [&](const std::string& name) {
                return prepared.value()->get_vector_property(name).value();
            };
            RayScene::Surface result{};
            result.parameters.y = material.get_surface().double_sided ? 1.f : 0.f;
            result.opacity.y = material.get_surface().alpha_cutoff;
            result.opacity.z = static_cast<float>(material.get_surface().alpha_mode);
            if(material.get_template_name() == "unlit_color") {
                result.emission_roughness = {Math::Vec3(vector("color")) * scalar("intensity"), -1};
                result.opacity.x = vector("color").a;
            } else {
                result.albedo_metallic = {Math::Vec3(vector("base_color")), scalar("metallic")};
                result.opacity.x = vector("base_color").a;
                result.emission_roughness = {
                    Math::Vec3(vector("emissive_color")) * scalar("emissive_strength"),
                    scalar("roughness")};
                result.parameters.x = scalar("normal_scale");
                result.parameters.z = scalar("transmission");
                result.parameters.w = scalar("ior");
                const std::array<std::string, 4> properties{"base_color_texture", "normal_texture",
                    "metallic_roughness_texture", "emissive_texture"};
                for(size_t i = 0; i < properties.size(); ++i) {
                    auto index = textures.add(material, properties[i]);
                    if(!index)
                        return Surface::failure(index.error());
                    result.textures[i] = index.value();
                }
            }
            if(!Math::is_finite(result.albedo_metallic)
                || !Math::is_finite(result.emission_roughness)
                || !Math::is_finite(result.parameters) || !Math::is_finite(result.opacity)
                || glm::any(glm::lessThan(Math::Vec3(result.emission_roughness), Math::Vec3(0))))
                return Surface::failure("射线场景材质包含无效参数");
            return Surface::success(result);
        }

        BoundingBox triangle_bounds(std::span<const RayScene::Triangle> triangles) {
            auto result = BoundingBox::from_point(Math::Vec3(triangles.front().positions[0]));
            for(const auto& triangle : triangles)
                for(const auto& position : triangle.positions)
                    result.include(Math::Vec3(position));
            return result;
        }

        void build_node(RayScene::Geometry& scene, uint32_t first, uint32_t count) {
            const auto index = scene.nodes.size();
            const auto box = triangle_bounds(std::span(scene.triangles).subspan(first, count));
            scene.nodes.push_back(
                {Math::Vec4(box.minimum, 0), Math::Vec4(box.maximum, 0), {first, count, 0, 0}});
            if(count > 4) {
                const auto extent = box.size();
                int axis = extent.y > extent.x ? 1 : 0;
                if(extent.z > extent[axis])
                    axis = 2;
                const auto middle = first + count / 2;
                std::nth_element(scene.triangles.begin() + first, scene.triangles.begin() + middle,
                    scene.triangles.begin() + first + count, [axis](const auto& a, const auto& b) {
                        return a.positions[0][axis] + a.positions[1][axis] + a.positions[2][axis]
                               < b.positions[0][axis] + b.positions[1][axis] + b.positions[2][axis];
                    });
                scene.nodes[index].links[1] = 0;
                build_node(scene, first, middle - first);
                build_node(scene, middle, first + count - middle);
            }
            scene.nodes[index].links[3] = static_cast<uint32_t>(scene.nodes.size());
        }

        struct MeshTransform {
            Math::Mat4 model;
            Math::Mat3 normal;
            float orientation;
            explicit MeshTransform(const Math::Mat4& matrix)
                : model(matrix), normal(glm::transpose(glm::inverse(Math::Mat3(matrix)))),
                  orientation(glm::determinant(Math::Mat3(matrix)) < 0 ? -1.f : 1.f) {}
        };
        bool valid_transform(const Math::Mat4& matrix) {
            return Math::is_finite(matrix)
                   && std::abs(glm::determinant(Math::Mat3(matrix))) >= 1e-10f;
        }
        Result<void> transform_triangle(
            RayScene::Triangle& triangle, const MeshData& mesh, const MeshTransform& transform) {
            for(size_t i = 0; i < 3; ++i) {
                // 对应光栅的镜像 front-face 切换，保持几何正面与变换后的法线同向。
                const auto source = transform.orientation < 0 && i > 0 ? 3 - i : i;
                const auto& vertex = mesh.vertices[triangle.surface[source + 1]];
                triangle.positions[i] = transform.model * Math::Vec4(vertex.position, 1);
                auto normal = Math::normalized_direction(transform.normal * vertex.normal);
                triangle.normals[i] = Math::Vec4(normal.value_or(Math::Vec3(0)), vertex.texcoord.x);
                Math::Vec3 tangent{}, bitangent{};
                if(normal && vertex.tangent.w != 0) {
                    const auto transformed =
                        Math::Vec3(transform.model * Math::Vec4(Math::Vec3(vertex.tangent), 0));
                    if(const auto direction = Math::normalized_direction(
                           transformed - *normal * glm::dot(*normal, transformed))) {
                        tangent = *direction;
                        bitangent =
                            glm::cross(*normal, tangent) * vertex.tangent.w * transform.orientation;
                    }
                }
                triangle.tangents[i] = Math::Vec4(tangent, vertex.texcoord.y);
                triangle.bitangents[i] = Math::Vec4(bitangent, 0);
                if(!Math::is_finite(vertex.normal) || !Math::is_finite(vertex.tangent)
                    || !Math::is_finite(triangle.positions[i])
                    || !Math::is_finite(triangle.normals[i])
                    || !Math::is_finite(triangle.tangents[i])
                    || !Math::is_finite(triangle.bitangents[i]))
                    return Result<void>::failure("射线场景网格包含非有限顶点");
            }
            return Result<void>::success();
        }

        Result<std::shared_ptr<const RayScene::Geometry>> build_geometry(
            const RenderWorld& world, const RaySceneBuilder::MeshReader& read) {
            using Built = Result<std::shared_ptr<const RayScene::Geometry>>;
            if(!read)
                return Built::failure("射线场景没有 CPU 网格读取服务");
            auto result = std::make_shared<RayScene::Geometry>();
            auto topology = std::make_shared<RayScene::Geometry::Topology>();
            std::map<AssetHandle, std::shared_ptr<const MeshData>> meshes;
            for(const auto& item : world.render_items) {
                if(!valid_transform(item.model_matrix))
                    return Built::failure("射线场景物体变换无效或不可逆");
                if(!meshes.contains(item.mesh_handle)) {
                    auto data = read(item.mesh_handle);
                    if(!data)
                        return Built::failure(data.error().message);
                    meshes.emplace(
                        item.mesh_handle, std::make_shared<MeshData>(std::move(data).value()));
                }
                const auto source = meshes.at(item.mesh_handle);
                const auto& mesh = *source;
                if(!mesh.skin.empty())
                    return Built::failure("射线场景首版仅接受非蒙皮网格");
                const auto vertices =
                    mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
                if(vertices % 3 != 0
                    || vertices / 3 > RayScene::MAX_TRIANGLES - result->triangles.size())
                    return Built::failure("射线场景三角形不完整或超过 262144 个预算");
                const MeshTransform transform(item.model_matrix);
                for(size_t first = 0; first < vertices; first += 3) {
                    RayScene::Triangle triangle;
                    triangle.surface[0] = static_cast<uint32_t>(topology->meshes.size());
                    for(size_t i = 0; i < 3; ++i) {
                        const auto index =
                            mesh.indices.empty() ? first + i : mesh.indices[first + i];
                        if(index >= mesh.vertices.size())
                            return Built::failure("射线场景网格索引越界");
                        triangle.surface[i + 1] = static_cast<uint32_t>(index);
                    }
                    if(auto transformed = transform_triangle(triangle, mesh, transform);
                        !transformed)
                        return Built::failure(transformed.error());
                    // 退化判定留在源空间，避免改变缩放时改变拓扑与发光面索引。
                    const auto& a = mesh.vertices[triangle.surface[1]].position;
                    const auto& b = mesh.vertices[triangle.surface[2]].position;
                    const auto& c = mesh.vertices[triangle.surface[3]].position;
                    const auto geometric = glm::cross(b - a, c - a);
                    if(glm::dot(geometric, geometric) >= 1e-16f)
                        result->triangles.push_back(triangle);
                }
                topology->meshes.push_back(source);
                result->transforms.push_back(item.model_matrix);
            }
            result->topology = std::move(topology);
            if(!result->triangles.empty())
                build_node(*result, 0, static_cast<uint32_t>(result->triangles.size()));
            return Built::success(std::move(result));
        }

        Result<std::shared_ptr<const RayScene::Geometry>> refit_geometry(
            const RenderWorld& world, const RayScene::Geometry& previous) {
            using Built = Result<std::shared_ptr<const RayScene::Geometry>>;
            auto result = std::make_shared<RayScene::Geometry>(previous);
            std::vector<std::optional<MeshTransform>> transforms(world.render_items.size());
            for(size_t i = 0; i < world.render_items.size(); ++i) {
                const auto& matrix = world.render_items[i].model_matrix;
                if(matrix == previous.transforms[i])
                    continue;
                if(!valid_transform(matrix))
                    return Built::failure("射线场景物体变换无效或不可逆");
                result->transforms[i] = matrix;
                transforms[i].emplace(matrix);
            }
            for(auto& triangle : result->triangles) {
                const auto object = triangle.surface[0];
                if(!transforms[object])
                    continue;
                if(auto transformed = transform_triangle(
                       triangle, *previous.topology->meshes[object], *transforms[object]);
                    !transformed)
                    return Built::failure(transformed.error());
            }
            // 节点按前序存储；逆序时两个子树的界限都已更新，不改变叶与三角形顺序。
            for(size_t i = result->nodes.size(); i-- > 0;) {
                auto& node = result->nodes[i];
                BoundingBox box;
                const auto count = node.links[1];
                if(count) {
                    box =
                        triangle_bounds(std::span(result->triangles).subspan(node.links[0], count));
                } else {
                    const auto& left = result->nodes[i + 1];
                    const auto& right = result->nodes[left.links[3]];
                    box = {glm::min(Math::Vec3(left.minimum), Math::Vec3(right.minimum)),
                        glm::max(Math::Vec3(left.maximum), Math::Vec3(right.maximum))};
                }
                node.minimum = Math::Vec4(box.minimum, 0);
                node.maximum = Math::Vec4(box.maximum, 0);
            }
            return Built::success(std::move(result));
        }
        Result<std::shared_ptr<const RayScene::Materials>> build_materials(const RenderWorld& world,
            const RaySceneBuilder::Images& images, uint32_t maximum_dimension) {
            using Built = Result<std::shared_ptr<const RayScene::Materials>>;
            auto result = std::make_shared<RayScene::Materials>();
            MaterialRuntimeCache cache;
            TexturePacker textures(*result, images, maximum_dimension);
            // 每个实例使用稳定槽位，换材质不改变 BVH 中的几何记录。
            for(const auto& item : world.render_items) {
                auto value = surface(item.material, cache, textures);
                if(!value)
                    return Built::failure(value.error());
                result->surfaces.push_back(value.value());
            }
            return Built::success(std::move(result));
        }

    }

    Result<std::shared_ptr<const RayScene>> RaySceneBuilder::update(const RenderWorld& world,
        const MeshReader& read, const MeshRevision& revision, const Images& images) {
        auto keys = geometry_keys(world, revision);
        const bool topology_changed = update_key(m_topology_key, std::move(keys.topology));
        const bool transforms_changed = update_key(m_geometry_key, std::move(keys.transforms));
        const bool geometry_changed = topology_changed || transforms_changed;
        const bool materials_changed = update_key(m_material_key, material_key(world, images));
        const bool environment_changed =
            update_key(m_environment_key, environment_key(world, images));
        const bool lighting_changed = update_key(m_lighting_key, lighting_key(world));
        const bool settings_changed = update_key(m_settings_key, settings_key(world.environment));
        if(!geometry_changed && !materials_changed && !environment_changed && !lighting_changed
            && !settings_changed && (m_scene || !m_error.empty())) {
            if(!m_scene)
                return Result<std::shared_ptr<const RayScene>>::failure(m_error);
            return Result<std::shared_ptr<const RayScene>>::success(m_scene);
        }
        const auto previous = std::exchange(m_scene, nullptr);
        // 键记录本次输入；失败的领域保留空缓存，避免之后改灯时复用过期内容。
        const auto previous_geometry =
            geometry_changed ? std::exchange(m_geometry, nullptr) : nullptr;
        if(materials_changed)
            m_materials.reset();
        if(environment_changed)
            m_environment.reset();
        if(lighting_changed)
            m_lighting.reset();
        const auto build = [&]() -> Result<std::shared_ptr<const RayScene>> {
            using Built = Result<std::shared_ptr<const RayScene>>;
            if(auto valid = world.environment.validate(); !valid)
                return Built::failure(valid.error());
            if(!m_geometry) {
                const bool refit =
                    !topology_changed && previous_geometry && previous_geometry->topology;
                auto candidate =
                    refit ? refit_geometry(world, *previous_geometry) : build_geometry(world, read);
                if(!candidate)
                    return Built::failure(candidate.error());
                m_geometry = candidate.value();
                if(refit)
                    ++m_statistics.geometry_refits;
                else
                    ++m_statistics.geometry_builds;
            }
            if(!m_materials) {
                auto candidate = build_materials(world, images, m_texture_dimension);
                if(!candidate)
                    return Built::failure(candidate.error());
                m_materials = candidate.value();
                ++m_statistics.material_builds;
            }
            if(!m_environment) {
                auto candidate = build_environment(world.environment, images);
                if(!candidate)
                    return Built::failure(candidate.error());
                m_environment = candidate.value();
                ++m_statistics.environment_builds;
            }
            const auto texture_bytes =
                (m_materials->texels.size() + m_environment->texels.size()) * sizeof(Math::Vec4);
            if(texture_bytes > RayScene::MAX_TEXTURE_BYTES)
                return Built::failure("射线场景材质与环境超过解码纹理总预算");
            auto scene = std::make_shared<RayScene>();
            scene->geometry = m_geometry;
            scene->materials = m_materials;
            scene->environment_map = m_environment;
            if(!m_lighting) {
                auto prepared = std::make_shared<LightingData>(LightingData::prepare(world.lights));
                if(prepared->excess_lights || prepared->invalid_lights)
                    return Built::failure("射线场景灯光无效或超过 32 盏预算");
                m_lighting = std::move(prepared);
            }
            scene->lighting = m_lighting;
            scene->background = world.environment.background_color;
            const float rotation = Math::radians(Math::wrap_degrees(world.environment.rotation));
            scene->environment = {world.environment.background ? world.environment.intensity : -1.f,
                world.environment.lighting ? world.environment.lighting_intensity : 0.f,
                std::sin(rotation), std::cos(rotation)};
            if(previous && previous->geometry->topology == m_geometry->topology
                && previous->materials == m_materials) {
                scene->emitters = previous->emitters;
            } else {
                auto emitters = std::make_shared<std::vector<uint32_t>>();
                for(uint32_t i = 0; i < m_geometry->triangles.size(); ++i) {
                    const auto slot = m_geometry->triangles[i].surface[0];
                    if(glm::any(glm::greaterThan(
                           Math::Vec3(m_materials->surfaces[slot].emission_roughness),
                           Math::Vec3(0))))
                        emitters->push_back(i);
                }
                scene->emitters = std::move(emitters);
            }
            return Built::success(std::move(scene));
        };
        auto result = build();
        m_scene = result ? result.value() : nullptr;
        m_error = result ? std::string{} : result.error();
        return result;
    }

    void RaySceneBuilder::reset() {
        m_settings_key.clear();
        m_geometry_key.clear();
        m_topology_key.clear();
        m_material_key.clear();
        m_environment_key.clear();
        m_lighting_key.clear();
        m_geometry.reset();
        m_materials.reset();
        m_environment.reset();
        m_lighting.reset();
        m_scene.reset();
        m_error.clear();
    }
}
