#include "render/material/material_renderer.h"

#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/scene/draw_order.h"
#include "render/scene/render_geometry.h"
#include "common/scope_exit.h"

#include <algorithm>
#include <functional>
#include <tuple>

namespace Comet {
    const std::shared_ptr<const MaterialRenderer::PipelineState>& MaterialRenderer::find_pipeline(
        const Material& material) const {
        if(material.get_shader_program()) {
            const auto found = m_project_pipelines.find(
                {material.get_shader_program(), material.get_template_name()});
            if(found != m_project_pipelines.end())
                return found->second.pipeline;
        } else {
            const auto found = m_pipelines.find(material.get_template_name());
            if(found != m_pipelines.end())
                return found->second;
        }
        static const std::shared_ptr<const PipelineState> unavailable;
        return unavailable;
    }

    bool MaterialRenderer::can_cull(const MaterialBinding& material) const {
        if(!material.resource)
            return false;
        const auto& pipeline = find_pipeline(*material.resource);
        if(!pipeline || !pipeline->static_mesh_bounds)
            return false;
        const auto cached = m_materials.find(DrawOrder::material_key(material));
        // 准备失败可能沿用旧管线；候选与回退都必须适用静态界限。
        return cached == m_materials.end() || !cached->second.resources
               || cached->second.resources->pipeline->static_mesh_bounds;
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_draw_queue(
        const RenderGeometry& geometry, const uint64_t frame_serial, const Frustum* frustum) {
        m_instance_transforms.clear();
        m_draw_queue.clear();
        const ScopeExit release_candidates([&] { m_draw_candidates.clear(); });
        collect_visible_draws(geometry, frustum);
        return prepare_draw_materials(frame_serial);
    }

    void MaterialRenderer::collect_visible_draws(
        const RenderGeometry& geometry, const Frustum* frustum) {
        auto& candidates = m_draw_candidates;
        candidates.clear();
        candidates.reserve(geometry.get_items().size());
        const MaterialBinding* previous_material = nullptr;
        bool cull = false;
        bool marked_hidden = false;
        for(const auto& prepared : geometry.get_items()) {
            const auto& item = *prepared.source;
            ++m_statistics.render_items;
            if(frustum
                && (!previous_material
                    || !DrawOrder::same_material_input(*previous_material, item.material))) {
                previous_material = &item.material;
                cull = can_cull(item.material);
                marked_hidden = false;
            }
            if(frustum && cull && prepared.world_bounds
                && !frustum->intersects(*prepared.world_bounds)) {
                ++m_statistics.culled_items;
                if(!marked_hidden) {
                    const auto key = DrawOrder::material_key(item.material);
                    if(const auto cached = m_materials.find(key); cached != m_materials.end())
                        cached->second.used = true;
                    m_prepared.mark_used(key);
                    marked_hidden = true;
                }
                continue;
            }
            candidates.push_back(prepared.source);
        }
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_draw_materials(
        const uint64_t frame_serial) {
        auto& candidates = m_draw_candidates;
        DrawOrder::sort_if_needed(candidates,
            [](const auto* a, const auto* b) { return DrawOrder::by_material(*a, *b); });
        for(size_t first = 0; first < candidates.size();) {
            const auto& input = candidates[first]->material;
            size_t end = first + 1;
            while(end < candidates.size()) {
                const auto& next = candidates[end]->material;
                if(!DrawOrder::same_material_input(input, next))
                    break;
                ++end;
            }
            auto material = prepare_material(input, frame_serial);
            if(!material) {
                m_draw_queue.clear();
                return Result<void, GraphicsError>::failure(material.error());
            }
            if(material.value()) {
                append_material_draws(
                    std::span(candidates).subspan(first, end - first), material.value());
            }
            first = end;
        }
        auto& queue = m_draw_queue;
        // 失败回退可能使用旧布局，按实际准备结果排序。
        const auto draw_less = [](const DrawItem& a, const DrawItem& b) {
            const auto a_instance = DrawOrder::material_key(a.item->material).instance_id;
            const auto b_instance = DrawOrder::material_key(b.item->material).instance_id;
            const auto a_key = std::tie(a.material->prepared->layout->get_name(),
                a.item->material.material_handle, a_instance);
            const auto b_key = std::tie(b.material->prepared->layout->get_name(),
                b.item->material.material_handle, b_instance);
            if(a_key != b_key)
                return a_key < b_key;
            const auto* a_mesh =
                a.material->pipeline->instanced_pipeline ? a.item->mesh.get() : nullptr;
            const auto* b_mesh =
                b.material->pipeline->instanced_pipeline ? b.item->mesh.get() : nullptr;
            if(a_mesh != b_mesh)
                return std::less<const Mesh*>{}(a_mesh, b_mesh);
            return std::less<const ResolvedRenderItem*>{}(a.item, b.item);
        };
        DrawOrder::sort_if_needed(queue, draw_less);
        return Result<void, GraphicsError>::success();
    }

    void MaterialRenderer::append_material_draws(const std::span<const ResolvedRenderItem*> items,
        const std::shared_ptr<MaterialResources>& material) {
        const bool instanced = material->pipeline->instanced_pipeline != nullptr;
        // 有序 Mesh 组直接合批；项目顶点程序保持原提交顺序。
        if(instanced) {
            DrawOrder::sort_if_needed(
                items, [](const auto* a, const auto* b) { return DrawOrder::by_mesh(*a, *b); });
        }
        for(size_t first = 0; first < items.size();) {
            size_t end = first + 1;
            if(instanced) {
                while(end < items.size() && items[end]->mesh == items[first]->mesh)
                    ++end;
            }
            DrawItem draw{items[first], material};
            draw.instance_count = static_cast<uint32_t>(end - first);
            if(draw.instance_count > 1) {
                draw.first_instance = static_cast<uint32_t>(m_instance_transforms.size());
                for(size_t index = first; index < end; ++index)
                    m_instance_transforms.push_back(items[index]->model_matrix);
            }
            m_draw_queue.push_back(std::move(draw));
            first = end;
        }
    }

}
