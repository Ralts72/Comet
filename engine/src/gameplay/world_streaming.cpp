#include "gameplay/world_streaming.h"
#include "scene/scene.h"
#include "scene/prefab.h"
#include "scene/component_registry.h"
#include "diagnostics/logger.h"
#include <algorithm>
#include <cmath>

namespace Comet::Gameplay {
    struct WorldStreamingSystem::Cell {
        AssetHandle source;
        EntityUuid root;
        std::shared_ptr<const Prefab> prefab;
        std::vector<AssetReference> references;
        std::string error;
        bool seen = false;
    };
    WorldStreamingSystem::WorldStreamingSystem(Sources sources, uint64_t budget)
        : m_sources(std::move(sources)), m_budget_bytes(budget),
          m_components(std::make_shared<ComponentRegistry>(create_scene_component_registry())) {}
    WorldStreamingSystem::~WorldStreamingSystem() = default;
    bool WorldStreamingSystem::unload(Scene& scene, Cell& cell) {
        if(!cell.root && !cell.prefab && cell.references.empty() && cell.error.empty())
            return true;
        if(auto root = scene.find_entity(cell.root); root && !scene.request_destroy_entity(root))
            return false;
        cell.root = {};
        cell.prefab.reset();
        cell.references.clear();
        cell.error.clear();
        ++m_revision;
        return true;
    }
    Result<void, Error> WorldStreamingSystem::post_update(Scene& scene, const Context&) {
        struct Demand {
            EntityUuid id;
            StreamCellComponent settings;
            Math::Vec3 position;
            float distance = 0;
        };
        std::vector<Demand> demands;
        scene.update_world_transforms();
        scene.each<const StreamCellComponent>([&](Entity entity, const auto& cell) {
            if(cell.enabled)
                demands.push_back(
                    {entity.get_uuid(), cell, Math::Vec3(scene.get_world_matrix(entity)[3]), 0});
        });
        for(auto& [id, cell] : m_cells)
            cell->seen = false;
        Entity camera;
        if(!demands.empty())
            scene.each<const CameraComponent>([&](Entity entity, const auto& value) {
                if(!camera || (value.primary && !camera.get_component<CameraComponent>().primary)
                    || (value.primary == camera.get_component<CameraComponent>().primary
                        && entity.get_id() < camera.get_id()))
                    camera = entity;
            });
        if(camera) {
            const auto position = Math::Vec3(scene.get_world_matrix(camera)[3]);
            for(auto& demand : demands)
                demand.distance = Math::length(demand.position - position);
            std::ranges::sort(demands, [](const auto& a, const auto& b) {
                return a.distance < b.distance || (a.distance == b.distance && a.id < b.id);
            });
        }
        // 先撤回旧单元需求；让宿主在下一帧释放资产后，再准备新区域。
        bool retiring = false;
        for(const auto& demand : demands) {
            auto& owner = m_cells[demand.id];
            if(!owner)
                owner = std::make_unique<Cell>();
            auto& cell = *owner;
            cell.seen = true;
            if(cell.source == demand.settings.prefab && camera
                && demand.distance <= demand.settings.unload_distance)
                continue;
            const bool held_resources = cell.root || cell.prefab || !cell.references.empty();
            if(unload(scene, cell)) {
                retiring |= held_resources;
                cell.source = demand.settings.prefab;
            }
        }
        for(auto it = m_cells.begin(); it != m_cells.end();) {
            if(it->second->seen) {
                ++it;
                continue;
            }
            const bool held_resources =
                it->second->root || it->second->prefab || !it->second->references.empty();
            if(!unload(scene, *it->second)) {
                ++it;
                continue;
            }
            retiring |= held_resources;
            it = m_cells.erase(it);
        }
        if(retiring)
            return Result<void, Error>::success();
        unsigned prepared = 0;
        for(const auto& demand : demands) {
            auto& cell = *m_cells.at(demand.id);
            const auto& settings = demand.settings;
            if(!camera || demand.distance > settings.unload_distance
                || cell.source != settings.prefab)
                continue;
            if(!settings.prefab || !std::isfinite(settings.load_distance)
                || settings.load_distance <= 0 || !std::isfinite(settings.unload_distance)
                || settings.unload_distance <= settings.load_distance)
                return Result<void, Error>::failure(
                    {"流式单元需要有效 Prefab，卸载距离须大于加载距离"});
            if(cell.root && !scene.find_entity(cell.root)) {
                cell.root = {};
                ++m_revision;
            }
            if(cell.root || !cell.error.empty())
                continue;
            const auto fail = [&](std::string error) {
                if(!unload(scene, cell))
                    return;
                cell.error = std::move(error);
                LOG_WARN("流式单元 {}: {}", demand.id.to_string(), cell.error);
            };
            if(!cell.prefab) {
                if(demand.distance > settings.load_distance || prepared)
                    continue;
                ++prepared;
                if(!m_sources.load || !m_sources.prepare || !m_sources.ready) {
                    fail("未装配项目资产服务");
                    continue;
                }
                auto loaded = m_sources.load(settings.prefab);
                if(!loaded || !loaded.value()) {
                    fail(loaded ? "Prefab 不可用" : loaded.error());
                    continue;
                }
                Scene content;
                auto instance = loaded.value()->instantiate(content, *m_components);
                if(!instance) {
                    fail(instance.error());
                    continue;
                }
                bool nested = false;
                content.each<const StreamCellComponent>(
                    [&](Entity, const auto&) { nested = true; });
                if(nested) {
                    fail("流式单元的 Prefab 不能再声明流式单元");
                    continue;
                }
                auto references = m_sources.prepare(content);
                if(!references) {
                    fail(references.error().message);
                    continue;
                }
                cell.prefab = loaded.value();
                cell.references = std::move(references).value();
                cell.references.push_back({settings.prefab, AssetType::Prefab, true});
                ++m_revision;
            }
            auto ready = m_sources.ready(cell.references);
            if(!ready) {
                fail(ready.error().message);
                continue;
            }
            if(!ready.value())
                continue;
            if(m_sources.resident_bytes && m_sources.resident_bytes() > m_budget_bytes) {
                fail("GPU 资产驻留超过流式预算；保留已激活世界，拒绝新单元");
                continue;
            }
            const auto root =
                scene.request_instantiate_prefab(cell.prefab, m_components, demand.position);
            if(!root) {
                fail("场景结构请求已满");
                continue;
            }
            cell.root = *root;
            ++m_revision;
        }
        return Result<void, Error>::success();
    }
    void WorldStreamingSystem::on_stop(
        Scene& scene, RuntimeSession&, const RuntimeServices&) noexcept {
        for(auto& [id, cell] : m_cells)
            if(auto root = scene.find_entity(cell->root))
                scene.destroy_entity(root);
        m_cells.clear();
        ++m_revision;
    }
    std::vector<AssetReference> WorldStreamingSystem::references() const {
        std::vector<AssetReference> result;
        for(const auto& [id, cell] : m_cells)
            result.insert(result.end(), cell->references.begin(), cell->references.end());
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
    WorldStreamingSystem::Statistics WorldStreamingSystem::statistics() const {
        Statistics result{.gpu_bytes = m_sources.resident_bytes ? m_sources.resident_bytes() : 0,
            .budget_bytes = m_budget_bytes};
        for(const auto& [id, cell] : m_cells) {
            result.active += bool(cell->root);
            result.pending += !cell->root && bool(cell->prefab);
            result.failed += !cell->error.empty();
        }
        return result;
    }
}
