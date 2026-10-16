#include "render/path_trace_scene.h"

#include "common/binary.h"
#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/material/material_runtime.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Comet {
    Result<void> PathTraceSettings::validate() const {
        if(samples == 0 || samples > 4096 || bounces == 0 || bounces > 8 || max_dimension < 32
            || max_dimension > 1024)
            return Result<void>::failure(
                "路径追踪参数超出范围（采样 1～4096，反弹 1～8，尺寸 32～1024）");
        return Result<void>::success();
    }

    Math::Vec2u PathTraceSettings::image_size(Math::Vec2u output) const {
        const float scale =
            std::min(1.f, float(max_dimension) / std::max({output.x, output.y, 1u}));
        return {std::max(1u, uint32_t(output.x * scale)), std::max(1u, uint32_t(output.y * scale))};
    }

    namespace {
        void write(Binary::Writer& key, Math::Vec3 value) {
            for(int i = 0; i < 3; ++i)
                key.write_float(value[i]);
        }

        std::vector<std::byte> source_key(
            const RenderWorld& world, const PathTraceSceneBuilder::MeshRevision& revision) {
            Binary::Writer key;
            key.write_u64(world.scene_lifetime);
            key.write_u64(world.render_items.size());
            for(const auto& item : world.render_items) {
                key.write_u64(item.mesh_handle.value());
                key.write_u64(item.mesh_revision);
                key.write_u64(revision ? revision(item.mesh_handle) : 0);
                key.write_u64(item.material.material_handle.value());
                key.write_u64(item.material.asset_revision);
                key.write_u64(item.material.resource ? item.material.resource->get_revision() : 0);
                key.write_u32(bool(item.material.overrides));
                if(item.material.overrides) {
                    const auto& overrides = *item.material.overrides;
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
                for(int col = 0; col < 4; ++col)
                    for(int row = 0; row < 4; ++row)
                        key.write_float(item.model_matrix[col][row]);
                key.write_u32(bool(item.mesh));
            }
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
            const auto& environment = world.environment;
            write(key, environment.background_color);
            key.write_u64(environment.asset.value());
            key.write_u32(environment.background);
            key.write_u32(environment.lighting);
            key.write_u32(environment.fog_enabled);
            key.write_u32(world.particles && !world.particles->batches.empty());
            return key.data();
        }

        struct Surface {
            Math::Vec4 albedo_metallic{};
            Math::Vec4 emission_roughness{};
            bool double_sided = false;
        };

        Result<Surface> surface(const MaterialBinding& binding, MaterialRuntimeCache& cache) {
            if(!binding.resource)
                return Result<Surface>::failure("路径追踪材质尚未加载");
            const auto& material = *binding.resource;
            const auto layout = MaterialLayout::find_builtin(material.get_template_name());
            if(material.get_shader_program() || !layout
                || (material.get_template_name() != "pbr"
                    && material.get_template_name() != "unlit_color"))
                return Result<Surface>::failure("路径追踪首版仅支持内置 PBR／无光照材质");
            if(material.get_surface().alpha_mode != AlphaMode::Opaque)
                return Result<Surface>::failure("路径追踪首版暂不支持透明／裁切材质");
            auto prepared =
                cache.prepare(binding.material_handle, binding.resource, layout, binding.overrides);
            if(!prepared)
                return Result<Surface>::failure(prepared.error());
            for(const auto& property : layout->get_textures())
                if(material.get_texture_property(property.name))
                    return Result<Surface>::failure("路径追踪首版暂不支持材质贴图");
            const auto scalar = [&](const std::string& name) {
                return prepared.value()->get_scalar_property(name).value();
            };
            const auto vector = [&](const std::string& name) {
                return prepared.value()->get_vector_property(name).value();
            };
            Surface result;
            result.double_sided = material.get_surface().double_sided;
            if(material.get_template_name() == "unlit_color") {
                result.emission_roughness = {Math::Vec3(vector("color")) * scalar("intensity"), -1};
            } else {
                result.albedo_metallic = {
                    glm::clamp(Math::Vec3(vector("base_color")), Math::Vec3(0), Math::Vec3(1)),
                    std::clamp(scalar("metallic"), 0.f, 1.f)};
                result.emission_roughness = {
                    Math::Vec3(vector("emissive_color")) * scalar("emissive_strength"),
                    std::clamp(scalar("roughness"), .045f, 1.f)};
            }
            if(!Math::is_finite(result.albedo_metallic)
                || !Math::is_finite(result.emission_roughness)
                || glm::any(glm::lessThan(Math::Vec3(result.emission_roughness), Math::Vec3(0))))
                return Result<Surface>::failure("路径追踪材质包含无效参数");
            return Result<Surface>::success(result);
        }

        BoundingBox bounds(const PathTraceScene::Triangle& triangle) {
            auto result = BoundingBox::from_point(Math::Vec3(triangle.positions[0]));
            result.include(Math::Vec3(triangle.positions[1]));
            result.include(Math::Vec3(triangle.positions[2]));
            return result;
        }

        void build_node(PathTraceScene& scene, uint32_t first, uint32_t count) {
            const auto index = scene.nodes.size();
            auto box = bounds(scene.triangles[first]);
            for(uint32_t i = 1; i < count; ++i)
                box.include(bounds(scene.triangles[first + i]));
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

        Result<std::shared_ptr<const PathTraceScene>> build(
            const RenderWorld& world, const PathTraceSceneBuilder::MeshReader& read) {
            using Built = Result<std::shared_ptr<const PathTraceScene>>;
            if(!read)
                return Built::failure("路径追踪没有 CPU 网格读取服务");
            if(auto valid = world.environment.validate(); !valid)
                return Built::failure(valid.error());
            if(world.environment.asset
                && (world.environment.background || world.environment.lighting))
                return Built::failure("路径追踪首版暂不支持 HDR 环境贴图，请使用常量背景");
            if(world.environment.fog_enabled
                || (world.particles && !world.particles->batches.empty()))
                return Built::failure("路径追踪首版暂不支持雾／粒子");
            auto result = std::make_shared<PathTraceScene>();
            result->background = world.environment.background_color;
            result->lighting = LightingData::prepare(world.lights);
            if(result->lighting.excess_lights || result->lighting.invalid_lights)
                return Built::failure("路径追踪灯光无效或超过 32 盏预算");
            std::map<AssetHandle, MeshData> meshes;
            MaterialRuntimeCache materials;
            for(const auto& item : world.render_items) {
                if(!Math::is_finite(item.model_matrix)
                    || std::abs(glm::determinant(Math::Mat3(item.model_matrix))) < 1e-10f)
                    return Built::failure("路径追踪物体变换无效或不可逆");
                auto material = surface(item.material, materials);
                if(!material)
                    return Built::failure(material.error());
                if(!meshes.contains(item.mesh_handle)) {
                    auto data = read(item.mesh_handle);
                    if(!data)
                        return Built::failure(data.error().message);
                    meshes.emplace(item.mesh_handle, std::move(data).value());
                }
                const auto& mesh = meshes.at(item.mesh_handle);
                if(!mesh.skin.empty())
                    return Built::failure("路径追踪首版仅接受静态网格，暂不支持蒙皮");
                const auto vertices =
                    mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
                if(vertices % 3 != 0
                    || vertices / 3 > PathTraceScene::MAX_TRIANGLES - result->triangles.size())
                    return Built::failure("路径追踪三角形不完整或超过 262144 个预算");
                const auto normal_matrix =
                    glm::transpose(glm::inverse(Math::Mat3(item.model_matrix)));
                for(size_t first = 0; first < vertices; first += 3) {
                    PathTraceScene::Triangle triangle;
                    for(size_t i = 0; i < 3; ++i) {
                        const auto index =
                            mesh.indices.empty() ? first + i : mesh.indices[first + i];
                        if(index >= mesh.vertices.size())
                            return Built::failure("路径追踪网格索引越界");
                        const auto& vertex = mesh.vertices[index];
                        triangle.positions[i] = item.model_matrix * Math::Vec4(vertex.position, 1);
                        triangle.normals[i] = Math::Vec4(normal_matrix * vertex.normal, 0);
                        if(!Math::is_finite(triangle.positions[i])
                            || !Math::is_finite(triangle.normals[i]))
                            return Built::failure("路径追踪网格包含非有限顶点");
                    }
                    const auto geometric =
                        glm::cross(Math::Vec3(triangle.positions[1] - triangle.positions[0]),
                            Math::Vec3(triangle.positions[2] - triangle.positions[0]));
                    if(glm::dot(geometric, geometric) < 1e-16f)
                        continue;
                    triangle.positions[0].w = material.value().double_sided ? 1.f : 0.f;
                    triangle.albedo_metallic = material.value().albedo_metallic;
                    triangle.emission_roughness = material.value().emission_roughness;
                    result->triangles.push_back(triangle);
                }
            }
            if(!result->triangles.empty())
                build_node(*result, 0, static_cast<uint32_t>(result->triangles.size()));
            for(uint32_t i = 0; i < result->triangles.size(); ++i)
                if(glm::any(glm::greaterThan(
                       Math::Vec3(result->triangles[i].emission_roughness), Math::Vec3(0))))
                    result->emitters.push_back(i);
            return Built::success(std::move(result));
        }
    }

    Result<std::shared_ptr<const PathTraceScene>> PathTraceSceneBuilder::update(
        const RenderWorld& world, const MeshReader& read, const MeshRevision& revision) {
        auto key = source_key(world, revision);
        if(key != m_key || (!m_scene && m_error.empty())) {
            auto candidate = build(world, read);
            m_key = std::move(key);
            m_scene = candidate ? std::move(candidate).value() : nullptr;
            m_error = candidate ? std::string{} : candidate.error();
        }
        if(!m_scene)
            return Result<std::shared_ptr<const PathTraceScene>>::failure(m_error);
        return Result<std::shared_ptr<const PathTraceScene>>::success(m_scene);
    }

    void PathTraceSceneBuilder::reset() {
        m_key.clear();
        m_scene.reset();
        m_error.clear();
    }
}
