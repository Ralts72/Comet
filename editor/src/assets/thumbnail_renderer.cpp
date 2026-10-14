#include "assets/thumbnail_renderer.h"
#include "assets/asset_thumbnails.h"
#include "asset/asset_manager.h"
#include "asset/data/model_data.h"
#include "asset/default_assets.h"
#include "render/material/material.h"
#include "render/scene/render_geometry.h"
#include "render/scene/render_submission.h"
#include "render/resource/resource_factory.h"
#include "render/resource/mesh.h"

#include <glm/gtc/quaternion.hpp>
#include <numbers>

namespace CometEditor {
    using namespace Comet;
    namespace {
        MeshData material_sphere() {
            constexpr uint32_t rings = 24, segments = 48;
            MeshData data;
            for(uint32_t y = 0; y <= rings; ++y) {
                const float v = float(y) / rings;
                const float latitude = v * std::numbers::pi_v<float>;
                for(uint32_t x = 0; x <= segments; ++x) {
                    const float u = float(x) / segments;
                    const float longitude = u * 2 * std::numbers::pi_v<float>;
                    const Math::Vec3 normal{std::sin(latitude) * std::cos(longitude),
                        std::cos(latitude), std::sin(latitude) * std::sin(longitude)};
                    data.vertices.push_back({normal, {u, v}, normal,
                        {-std::sin(longitude), 0, std::cos(longitude), 1}});
                }
            }
            for(uint32_t y = 0; y < rings; ++y)
                for(uint32_t x = 0; x < segments; ++x) {
                    const auto a = y * (segments + 1) + x, b = a + segments + 1;
                    data.indices.insert(data.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
                }
            return data;
        }

        Result<void, GraphicsError> append_mesh(RenderWorld& world, AssetManager& assets,
            AssetHandle mesh, AssetHandle material, const Math::Mat4& transform) {
            auto geometry = assets.load_mesh(mesh);
            if(!geometry)
                return Result<void, GraphicsError>::failure({geometry.error().message});
            auto surface = assets.load_material(material);
            if(!surface)
                return Result<void, GraphicsError>::failure({surface.error().message});
            auto snapshot = surface.value()->snapshot();
            auto program = assets.compiled_shader_program(snapshot->get_shader_program());
            world.render_items.push_back({.entity_id = EntityId(world.render_items.size() + 1),
                .model_matrix = transform,
                .mesh_handle = mesh,
                .mesh = std::move(geometry).value(),
                .material = {.material_handle = material,
                    .resource = std::move(snapshot),
                    .program_source = std::move(program)}});
            return Result<void, GraphicsError>::success();
        }
    }

    ThumbnailRenderer::ThumbnailRenderer(Renderer& renderer, RenderResourceFactory& factory)
        : m_preview(renderer, Math::Vec2u(AssetThumbnails::IMAGE_SIZE)), m_factory(factory) {}
    Result<void, GraphicsError> ThumbnailRenderer::submit(
        AssetManager& assets, AssetHandle handle) {
        using Outcome = Result<void, GraphicsError>;
        if(m_preview.busy())
            return Outcome::failure({"Thumbnail is already being rendered"});
        const auto* record = assets.get_database().find(handle);
        if(!record)
            return Outcome::failure({"预览资源不存在"});
        auto world = std::make_shared<RenderWorld>();
        if(record->type == AssetType::Material) {
            auto material = assets.load_material(handle);
            if(!material)
                return Outcome::failure({material.error().message});
            auto snapshot = material.value()->snapshot();
            auto program = assets.compiled_shader_program(snapshot->get_shader_program());
            if(!m_material_sphere) {
                auto created = m_factory.try_create_mesh(material_sphere());
                if(!created)
                    return Outcome::failure(created.error());
                m_material_sphere = std::move(created).value();
            }
            world->render_items.push_back({.entity_id = EntityId(1),
                .mesh = m_material_sphere,
                .material = {.material_handle = handle,
                    .resource = std::move(snapshot),
                    .program_source = std::move(program)}});
        } else if(record->type == AssetType::Mesh) {
            if(auto added =
                    append_mesh(*world, assets, handle, DEFAULT_PBR_MATERIAL, Math::Mat4(1));
                !added)
                return added;
        } else if(record->type == AssetType::Model) {
            auto loaded = assets.load_model(handle);
            if(!loaded)
                return Outcome::failure({loaded.error().message});
            const auto& model = *loaded.value();
            std::vector<std::vector<uint32_t>> children(model.nodes.size());
            for(uint32_t i = 0; i < model.nodes.size(); ++i)
                if(model.nodes[i].parent != ModelData::NO_PARENT)
                    children.at(model.nodes[i].parent).push_back(i);
            std::vector<std::pair<uint32_t, Math::Mat4>> pending;
            for(const auto root : model.scenes.at(model.default_scene).roots)
                pending.emplace_back(root, Math::Mat4(1));
            while(!pending.empty()) {
                auto [index, parent] = pending.back();
                pending.pop_back();
                const auto& node = model.nodes.at(index);
                const auto& local = node.transform;
                const auto transform = parent * Math::translate(Math::Mat4(1), local.translation)
                                       * glm::mat4_cast(local.rotation)
                                       * Math::scale(Math::Mat4(1), local.scale);
                for(const auto& part : node.parts)
                    if(auto added = append_mesh(*world, assets, part.mesh,
                           part.material ? part.material : DEFAULT_PBR_MATERIAL, transform);
                        !added)
                        return added;
                for(const auto child : children[index])
                    pending.emplace_back(child, transform);
            }
        } else {
            return Outcome::failure({"资源类型不支持三维预览"});
        }
        RenderGeometry geometry;
        geometry.prepare(*world);
        const auto bounds = geometry.get_scene_bounds();
        if(!bounds)
            return Outcome::failure({"资源没有可预览的几何"});
        const float radius = std::max(glm::length(bounds->size()) * 0.5f, 0.001f);
        const auto normalize = Math::scale(Math::Mat4(1), Math::Vec3(1 / radius))
                               * Math::translate(Math::Mat4(1), -bounds->center());
        for(auto& item : world->render_items)
            item.model_matrix = normalize * item.model_matrix;
        world->lights = {{.direction = {-0.6f, -0.8f, -1}, .intensity = 4},
            {.direction = {0.7f, -0.2f, 0.5f}, .intensity = 2}};
        world->post_process.bloom_enabled = false;
        return m_preview.submit(
            std::move(world), {.view_matrix = Math::look_at({2.5f, 1.5f, 3}, {0, 0, 0}, {0, 1, 0}),
                                  .projection = RenderCamera::Projection::Orthographic,
                                  .orthographic_height = 2.4f,
                                  .near_clip = 0.1f,
                                  .far_clip = 10});
    }

}
