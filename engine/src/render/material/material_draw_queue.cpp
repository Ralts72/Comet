#include "render/material/material_renderer.h"

#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/scene/draw_order.h"
#include "render/scene/render_geometry.h"

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
        collect_visible_draws(geometry, frustum);
        return prepare_draw_materials(frame_serial);
    }

    void MaterialRenderer::collect_visible_draws(
        const RenderGeometry& geometry, const Frustum* frustum) {
        auto& queue = m_draw_queue;
        queue.clear();
        queue.reserve(geometry.get_items().size());
        const MaterialBinding* previous_material = nullptr;
        bool cull = false;
        for(const auto& prepared : geometry.get_items()) {
            const auto& item = *prepared.source;
            ++m_statistics.render_items;
            if(frustum
                && (!previous_material
                    || !DrawOrder::same_material_input(*previous_material, item.material))) {
                previous_material = &item.material;
                cull = can_cull(item.material);
            }
            if(frustum && cull && prepared.world_bounds
                && !frustum->intersects(*prepared.world_bounds)) {
                ++m_statistics.culled_items;
                const auto key = DrawOrder::material_key(item.material);
                if(const auto cached = m_materials.find(key); cached != m_materials.end())
                    cached->second.used = true;
                m_prepared.mark_used(key);
                continue;
            }
            queue.push_back({&item, nullptr});
        }
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_draw_materials(
        const uint64_t frame_serial) {
        auto& queue = m_draw_queue;
        const auto material_less = [](const DrawItem& a, const DrawItem& b) {
            return DrawOrder::by_material(*a.item, *b.item);
        };
        if(!std::is_sorted(queue.begin(), queue.end(), material_less))
            std::sort(queue.begin(), queue.end(), material_less);
        size_t batches = 0;
        for(size_t first = 0; first < queue.size();) {
            const auto& input = queue[first].item->material;
            size_t end = first + 1;
            while(end < queue.size()) {
                const auto& next = queue[end].item->material;
                if(!DrawOrder::same_material_input(input, next))
                    break;
                ++end;
            }
            auto material = prepare_material(input, frame_serial);
            if(!material) {
                queue.clear();
                return Result<void, GraphicsError>::failure(material.error());
            }
            if(material.value()) {
                append_material_draws(
                    std::span(queue).subspan(first, end - first), material.value(), batches);
            }
            first = end;
        }
        queue.resize(batches);
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
        if(!std::is_sorted(queue.begin(), queue.end(), draw_less))
            std::sort(queue.begin(), queue.end(), draw_less);
        return Result<void, GraphicsError>::success();
    }

    void MaterialRenderer::append_material_draws(const std::span<DrawItem> items,
        const std::shared_ptr<MaterialResources>& material, size_t& batches) {
        const bool instanced = material->pipeline->instanced_pipeline != nullptr;
        // 有序 Mesh 组直接合批；项目顶点程序保持原提交顺序。
        if(instanced) {
            const auto mesh_less = [](const DrawItem& a, const DrawItem& b) {
                return DrawOrder::by_mesh(*a.item, *b.item);
            };
            if(!std::is_sorted(items.begin(), items.end(), mesh_less))
                std::sort(items.begin(), items.end(), mesh_less);
        }
        for(size_t first = 0; first < items.size();) {
            size_t end = first + 1;
            if(instanced) {
                while(end < items.size() && items[end].item->mesh == items[first].item->mesh)
                    ++end;
            }
            DrawItem draw{items[first].item, material};
            draw.instance_count = static_cast<uint32_t>(end - first);
            if(draw.instance_count > 1) {
                draw.first_instance = static_cast<uint32_t>(m_instance_transforms.size());
                for(size_t index = first; index < end; ++index)
                    m_instance_transforms.push_back(items[index].item->model_matrix);
            }
            m_draw_queue[batches++] = std::move(draw);
            first = end;
        }
    }

}
