#include "render/scene/render_geometry.h"

#include "render/resource/mesh.h"
#include "render/scene/render_submission.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Comet {
    namespace {
        void include_bounds(
            std::optional<BoundingBox>& aggregate, const std::optional<BoundingBox>& bounds) {
            if(!bounds)
                return;
            if(aggregate)
                aggregate->include(*bounds);
            else
                aggregate = bounds;
        }
    }

    void RenderGeometry::prepare(const RenderWorld& world) {
        clear_frame();
        const auto& items = world.render_items;
        const auto& changes = world.item_changes;
        m_items.reserve(items.size());
        m_skin_bounds.resize(items.size());
        const bool same_scene =
            world.scene_lifetime != 0 && world.scene_lifetime == m_scene_version.identity;
        const bool tracked = same_scene && changes.revision != 0;
        const bool unchanged = tracked && m_scene_version.revision == changes.revision
                               && m_bounds.size() == items.size();
        const bool partial =
            tracked && changes.can_apply(m_scene_version.revision, m_bounds.size(), items.size());
        const auto update_bounds = [&](const std::size_t index) {
            const auto& item = items[index];
            auto& cached = m_bounds[index];
            if(!same_scene || item.entity_id == INVALID_ENTITY_ID || item.transform_revision == 0
                || cached.entity_id != item.entity_id
                || cached.transform_revision != item.transform_revision || !item.mesh_handle
                || item.mesh_revision == 0 || cached.mesh_revision != item.mesh_revision || item.lod
                || cached.had_lod) {
                cached.bounds.reset();
                if(item.mesh)
                    cached.bounds = transform_box(item.mesh->get_local_bounds(), item.model_matrix);
                if(item.lod)
                    for(const auto& mesh : item.lod->meshes)
                        if(mesh)
                            include_bounds(cached.bounds,
                                transform_box(mesh->get_local_bounds(), item.model_matrix));
                cached.had_lod = bool(item.lod);
                cached.entity_id = item.entity_id;
                cached.transform_revision = item.transform_revision;
                cached.mesh_revision = item.mesh_revision;
                return true;
            }
            return false;
        };
        bool rebuild = false;
        if(!unchanged && partial) {
            changes.apply_structure(m_bounds);
            // 删除可能收缩场景总界限；在填充本帧借用列表时重新汇总。
            rebuild = std::ranges::any_of(changes.structure, [](const auto& edit) {
                return edit.type == RenderItemChanges::StructuralEdit::Type::Remove;
            });
            const auto previous_bounds = m_cached_scene_bounds;
            for(const auto index : changes.slots) {
                const auto old = m_bounds[index].bounds;
                if(!update_bounds(index))
                    continue;
                const auto& bounds = m_bounds[index].bounds;
                // 向外扩张可直接合并；旧极值收缩时重新汇总所有界限。
                if(old && previous_bounds) {
                    for(int axis = 0; axis < 3; ++axis) {
                        rebuild |= old->minimum[axis] == previous_bounds->minimum[axis]
                                   && (!bounds || bounds->minimum[axis] > old->minimum[axis]);
                        rebuild |= old->maximum[axis] == previous_bounds->maximum[axis]
                                   && (!bounds || bounds->maximum[axis] < old->maximum[axis]);
                    }
                }
                include_bounds(m_cached_scene_bounds, bounds);
            }
        } else if(!unchanged) {
            if(same_scene)
                RenderItemChanges::align_single_edit(items, m_bounds);
            m_bounds.resize(items.size());
            m_cached_scene_bounds.reset();
            for(std::size_t index = 0; index < items.size(); ++index) {
                update_bounds(index);
                const auto& bounds = m_bounds[index].bounds;
                m_items.push_back({&items[index], bounds ? &*bounds : nullptr});
                m_lod_count += bool(items[index].lod);
                include_bounds(m_cached_scene_bounds, bounds);
            }
        }
        // 结构更新完成后借用本帧提交和缓存界限，避免每帧复制界限值。
        if(unchanged || partial) {
            if(rebuild)
                m_cached_scene_bounds.reset();
            for(std::size_t index = 0; index < items.size(); ++index) {
                const auto& bounds = m_bounds[index].bounds;
                m_items.push_back({&items[index], bounds ? &*bounds : nullptr});
                m_lod_count += bool(items[index].lod);
                if(rebuild)
                    include_bounds(m_cached_scene_bounds, bounds);
            }
        }
        m_scene_bounds = m_cached_scene_bounds;
        m_scene_version = {world.scene_lifetime, changes.revision};
    }

    float RenderGeometry::projected_height(const BoundingBox& bounds, const Math::Mat4& clip) {
        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();
        for(unsigned corner = 0; corner < 8; ++corner) {
            const Math::Vec3 point{(corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                (corner & 4) ? bounds.maximum.z : bounds.minimum.z};
            const auto projected = clip * Math::Vec4(point, 1);
            // 跨近裁剪面时使用原网格，避免透视除法产生跳级。
            if(!std::isfinite(projected.w) || projected.w <= 0 || projected.z < 0)
                return std::numeric_limits<float>::infinity();
            const float y = projected.y / projected.w;
            if(!std::isfinite(y))
                return std::numeric_limits<float>::infinity();
            low = std::min(low, y);
            high = std::max(high, y);
        }
        return (high - low) * 0.5f;
    }

    int RenderGeometry::select_lod(const MeshLod& settings, float height, int previous) {
        const auto finite = [](float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        };
        const float first = std::clamp(finite(settings.level1_height, 0.25f), 0.001f, 1.0f);
        const std::array thresholds{
            first, std::clamp(finite(settings.level2_height, 0.08f), 0.001f, first)};
        const float hysteresis = std::clamp(finite(settings.hysteresis, 0.1f), 0.0f, 0.5f);
        const std::array available{true, bool(settings.level1), bool(settings.level2)};
        int level = static_cast<int>(settings.preview);
        if(level >= 0 && level <= 2) {
            while(level > 0 && !available[level])
                --level;
            return level;
        }
        if(std::isnan(height))
            return 0;
        level = 0;
        for(int candidate = 1; candidate <= 2; ++candidate) {
            if(!available[candidate])
                continue;
            float threshold = thresholds[candidate - 1];
            if(previous >= 0)
                threshold *= previous >= candidate ? 1 + hysteresis : 1 - hysteresis;
            if(height < threshold)
                level = candidate;
        }
        return level;
    }

    const RenderGeometry& RenderGeometry::prepare_view(
        const RenderGeometry& world, const Math::Mat4& clip) {
        clear_frame();
        if(!world.m_scene_version.identity
            || m_scene_version.identity != world.m_scene_version.identity)
            m_lod_history.clear();
        m_scene_version = world.m_scene_version;
        if(world.m_lod_count == 0) {
            m_lod_history.clear();
            return world;
        }
        m_lod_count = world.m_lod_count;
        for(auto& [id, history] : m_lod_history)
            history.used = false;
        const auto inputs = world.get_items();
        m_items.reserve(inputs.size());
        // 预留后才保存指针，保证本帧各队列借用的地址稳定。
        m_selected_items.reserve(world.m_lod_count);
        for(const auto& input : inputs) {
            const auto& item = *input.source;
            if(!item.lod || !input.world_bounds) {
                m_items.push_back(input);
                continue;
            }
            const auto& lod = *item.lod;
            auto& history = m_lod_history[item.entity_id];
            if(history.settings != lod.settings || item.entity_id == INVALID_ENTITY_ID)
                history.level = -1;
            history.settings = lod.settings;
            history.level = select_lod(
                lod.settings, projected_height(*input.world_bounds, clip), history.level);
            history.used = true;
            if(history.level == 0) {
                m_items.push_back(input);
                continue;
            }
            auto& selected = m_selected_items.emplace_back(item);
            const auto index = history.level - 1;
            selected.mesh = lod.meshes[index];
            selected.mesh_revision = lod.revisions[index];
            selected.mesh_handle = index == 0 ? lod.settings.level1 : lod.settings.level2;
            m_items.push_back({&selected, input.world_bounds});
        }
        std::erase_if(m_lod_history, [](const auto& entry) { return !entry.second.used; });
        m_scene_bounds = world.m_scene_bounds;
        return *this;
    }

    void RenderGeometry::replace_skinned(
        std::size_t index, const ResolvedRenderItem& item, const BoundingBox& bounds) {
        m_skin_bounds[index] = bounds;
        m_items[index] = {&item, &m_skin_bounds[index]};
        if(m_scene_bounds)
            m_scene_bounds->include(bounds);
        else
            m_scene_bounds = bounds;
    }
    void RenderGeometry::clear_frame() {
        m_items.clear();
        m_lod_count = 0;
        m_selected_items.clear();
        m_scene_bounds.reset();
    }
    void RenderGeometry::reset() {
        clear_frame();
        m_bounds.clear();
        m_lod_history.clear();
        m_cached_scene_bounds.reset();
        m_scene_version = {};
    }

}
