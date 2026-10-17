#include "assets/terrain_editing.h"
#include "assets/editor_assets.h"
#include "asset/import/terrain_importer.h"
#include "render/debug/line_draw_list.h"
#include "scene/scene_commands.h"
#include "scene/scene.h"
#include "diagnostics/logger.h"
#include <algorithm>
#include <cmath>

namespace CometEditor {
    namespace {
        using namespace Comet;
        float sample(const TerrainData& data, Math::Vec2 point) {
            const auto grid = Math::clamp(
                (point / Math::Vec2(data.width, data.depth) + .5f) * float(data.resolution - 1),
                Math::Vec2(0), Math::Vec2(data.resolution - 1));
            const auto cell = Math::min(Math::Vec2i(grid), Math::Vec2i(data.resolution - 2));
            const auto f = grid - Math::Vec2(cell);
            const auto first = size_t(cell.y) * data.resolution + cell.x;
            const float a = data.heights[first], b = data.heights[first + 1],
                        c = data.heights[first + data.resolution],
                        d = data.heights[first + data.resolution + 1];
            return (f.x + f.y <= 1 ? a + (b - a) * f.x + (c - a) * f.y
                                   : d + (c - d) * (1 - f.x) + (b - d) * (1 - f.y))
                   * data.height_scale;
        }
        std::optional<float> triangle_hit(
            const Ray& ray, Math::Vec3 a, Math::Vec3 b, Math::Vec3 c) {
            const auto edge = b - a, other = c - a, p = Math::cross(ray.direction, other);
            const float determinant = Math::dot(edge, p);
            if(std::abs(determinant) < 1e-8f)
                return {};
            const auto offset = ray.origin - a;
            const float u = Math::dot(offset, p) / determinant;
            const auto q = Math::cross(offset, edge);
            const float v = Math::dot(ray.direction, q) / determinant;
            const float t = Math::dot(other, q) / determinant;
            if(u < -1e-5f || v < -1e-5f || u + v > 1.00001f || t < 0 || t > ray.max_parameter)
                return {};
            return t;
        }
        class TerrainStroke final: public CommandHistory::Command {
        public:
            TerrainStroke(EditorAssets& assets, const ComponentRegistry& components,
                AssetHandle asset, TerrainData before, TerrainData after)
                : m_assets(assets), m_components(components), m_asset(asset),
                  m_before(std::move(before)), m_after(std::move(after)) {}
            bool undo(Scene& scene) override { return apply(scene, false); }
            bool redo(Scene& scene) override { return apply(scene, true); }

        private:
            Result<std::shared_ptr<const ModelData>, Error> save(const TerrainData& value) {
                using Saved = Result<std::shared_ptr<const ModelData>, Error>;
                auto current =
                    m_assets.read_terrain({m_asset, m_assets.database().get_revision(m_asset)});
                if(!current)
                    return Saved::failure({current.error()});
                auto saved = m_assets.apply_terrain_edit(
                    {{m_asset, m_assets.database().get_revision(m_asset)},
                        TerrainEdit{std::move(current).value(), value}});
                if(!saved) {
                    LOG_ERROR("Terrain save: {}", saved.error().message);
                    return Saved::failure(saved.error());
                }
                auto model = m_assets.rebuild_terrain(m_asset);
                if(!model) {
                    LOG_ERROR("Terrain rebuild: {}", model.error().message);
                    return Saved::failure(model.error());
                }
                return model;
            }
            bool apply(Scene& scene, bool forward) {
                const auto& value = forward ? m_after : m_before;
                const auto& previous = forward ? m_before : m_after;
                auto model = save(value);
                if(!model) {
                    static_cast<void>(save(previous));
                    return false;
                }
                if(m_instances.empty()) {
                    auto prefab =
                        Prefab::from_model(*model.value(), m_asset, m_components, "Terrain");
                    if(!prefab) {
                        static_cast<void>(save(previous));
                        return false;
                    }
                    bool valid = true;
                    scene.each<const PrefabInstanceComponent>([&](Entity root, const auto& link) {
                        if(link.asset != m_asset || !valid)
                            return;
                        auto candidate = prefab.value().update_candidate(scene, m_components, root);
                        if(!candidate) {
                            valid = false;
                            return;
                        }
                        auto command = SceneCommands::replace_tree(
                            scene, m_components, root.get_uuid(), std::move(candidate).value());
                        if(!command) {
                            valid = false;
                            return;
                        }
                        m_instances.push_back(std::move(command));
                    });
                    if(!valid) {
                        m_instances.clear();
                        static_cast<void>(save(previous));
                        return false;
                    }
                }
                size_t applied = 0;
                for(auto& command : m_instances) {
                    if(!(forward ? command->redo(scene) : command->undo(scene))) {
                        while(applied) {
                            auto& rollback = m_instances[--applied];
                            static_cast<void>(
                                forward ? rollback->undo(scene) : rollback->redo(scene));
                        }
                        static_cast<void>(save(previous));
                        return false;
                    }
                    ++applied;
                }
                return true;
            }
            EditorAssets& m_assets;
            const ComponentRegistry& m_components;
            AssetHandle m_asset;
            TerrainData m_before, m_after;
            std::vector<std::unique_ptr<CommandHistory::Command>> m_instances;
        };
    }
    bool apply_terrain_brush(Comet::TerrainData& data, Comet::Math::Vec2 center,
        const TerrainBrush& brush, double delta, float flatten_height) {
        using namespace Comet;
        if(data.resolution < 2 || data.width <= 0 || data.depth <= 0 || data.height_scale <= 0
            || data.heights.size() != size_t(data.resolution) * data.resolution
            || (!data.layer_weights.empty() && data.layer_weights.size() != data.heights.size())
            || !Math::is_finite(center) || !std::isfinite(brush.radius) || brush.radius <= 0
            || !std::isfinite(brush.strength) || brush.strength < 0 || !std::isfinite(delta)
            || delta <= 0 || brush.layer >= data.layers.size())
            return false;
        std::vector<float> before;
        if(brush.mode == TerrainBrush::Mode::Smooth)
            before = data.heights;
        bool changed = false;
        const auto cells = float(data.resolution - 1);
        const auto extent = Comet::Math::Vec2(data.width, data.depth);
        const auto minimum = Math::clamp(
            Comet::Math::Vec2i(Math::floor(((center - brush.radius) / extent + .5f) * cells)),
            Comet::Math::Vec2i(0), Comet::Math::Vec2i(data.resolution - 1));
        const auto maximum = Math::clamp(
            Comet::Math::Vec2i(Math::ceil(((center + brush.radius) / extent + .5f) * cells)),
            Comet::Math::Vec2i(0), Comet::Math::Vec2i(data.resolution - 1));
        for(int z = minimum.y; z <= maximum.y; ++z)
            for(int x = minimum.x; x <= maximum.x; ++x) {
                const Math::Vec2 point{
                    (x / cells - .5f) * data.width, (z / cells - .5f) * data.depth};
                const float distance = Math::length(point - center) / brush.radius;
                if(distance >= 1)
                    continue;
                const float falloff = (1 - distance * distance) * (1 - distance * distance);
                const auto index = size_t(z) * data.resolution + x;
                if(brush.mode == TerrainBrush::Mode::Paint) {
                    data.initialize_layer_weights();
                    auto& weights = data.layer_weights[index];
                    Math::Vec3 target(0);
                    target[brush.layer] = 1;
                    const auto output = Math::mix(
                        weights, target, float(-std::expm1(-delta * brush.strength * falloff)));
                    changed |= weights != output;
                    weights = output;
                    continue;
                }
                const float value = data.heights[index];
                float output = value;
                if(brush.mode == TerrainBrush::Mode::Raise
                    || brush.mode == TerrainBrush::Mode::Lower)
                    output += float(delta) * brush.strength * falloff / data.height_scale
                              * (brush.mode == TerrainBrush::Mode::Raise ? 1.f : -1.f);
                else {
                    float target = flatten_height / data.height_scale;
                    if(brush.mode == TerrainBrush::Mode::Smooth) {
                        target = 0;
                        for(int dz = -1; dz <= 1; ++dz)
                            for(int dx = -1; dx <= 1; ++dx)
                                target +=
                                    before[size_t(
                                               std::clamp(int(z) + dz, 0, int(data.resolution) - 1))
                                               * data.resolution
                                           + std::clamp(int(x) + dx, 0, int(data.resolution) - 1)]
                                    / 9.f;
                    }
                    output = std::lerp(
                        value, target, float(-std::expm1(-delta * brush.strength * falloff)));
                }
                output = std::clamp(output, 0.f, 1.f);
                changed |= output != value;
                data.heights[index] = output;
            }
        return changed;
    }
    std::optional<Comet::Math::Vec3> intersect_terrain(
        const Comet::TerrainData& data, const Comet::Ray& ray) {
        using namespace Comet;
        if(!ray.is_valid() || data.heights.size() != size_t(data.resolution) * data.resolution)
            return {};
        const BoundingBox bounds{{-data.width * .5f, 0, -data.depth * .5f},
            {data.width * .5f, data.height_scale, data.depth * .5f}};
        const auto entry = intersect_ray_box(ray, bounds);
        if(!entry)
            return {};
        const Math::Vec2 spacing{
            data.width / (data.resolution - 1), data.depth / (data.resolution - 1)};
        const auto start = ray.origin + ray.direction * *entry;
        const Math::Vec2 grid{
            (start.x + data.width * .5f) / spacing.x, (start.z + data.depth * .5f) / spacing.y};
        auto cell =
            Math::clamp(Math::Vec2i(grid), Math::Vec2i(0), Math::Vec2i(data.resolution - 2));
        const Math::Vec2 direction{ray.direction.x, ray.direction.z},
            origin{ray.origin.x, ray.origin.z};
        const Math::Vec2 minimum{-data.width * .5f, -data.depth * .5f};
        const auto step = Math::Vec2i(Math::sign(direction));
        for(uint32_t visited = 0; visited < 2 * data.resolution; ++visited) {
            const auto at = [&](int x, int z) {
                return Math::Vec3(minimum.x + x * spacing.x,
                    data.heights[size_t(z) * data.resolution + x] * data.height_scale,
                    minimum.y + z * spacing.y);
            };
            const auto a = at(cell.x, cell.y), b = at(cell.x + 1, cell.y),
                       c = at(cell.x, cell.y + 1), d = at(cell.x + 1, cell.y + 1);
            auto first = triangle_hit(ray, a, c, b), second = triangle_hit(ray, b, c, d);
            if(first || second) {
                const float t = first && second ? std::min(*first, *second)
                                                : first.value_or(second.value_or(0));
                return ray.origin + ray.direction * t;
            }
            Math::Vec2 next(std::numeric_limits<float>::infinity());
            for(int axis = 0; axis < 2; ++axis)
                if(step[axis])
                    next[axis] =
                        (minimum[axis] + (cell[axis] + (step[axis] > 0 ? 1 : 0)) * spacing[axis]
                            - origin[axis])
                        / direction[axis];
            if(!std::isfinite(std::min(next.x, next.y))
                || std::min(next.x, next.y) > ray.max_parameter)
                break;
            if(next.x <= next.y)
                cell.x += step.x;
            else
                cell.y += step.y;
            if(Math::any_less(cell, Math::Vec2i(0))
                || Math::any_greater_equal(cell, Math::Vec2i(data.resolution - 1)))
                break;
        }
        return {};
    }
    TerrainEditing::TerrainEditing(
        EditorAssets& assets, CommandHistory& history, const Comet::ComponentRegistry& components)
        : m_assets(assets), m_history(history), m_components(components) {}
    Comet::Entity TerrainEditing::root(Comet::Entity entity) const {
        auto* scene = m_history.get_scene();
        if(!scene || !entity || scene->find_entity(entity.get_uuid()) != entity)
            return {};
        while(entity) {
            if(entity.has_component<Comet::PrefabInstanceComponent>()) {
                const auto asset = entity.get_component<Comet::PrefabInstanceComponent>().asset;
                const auto* record = m_assets.database().find(asset);
                if(record && record->path.extension() == ".terrain")
                    return entity;
            }
            entity = scene->get_parent(entity);
        }
        return {};
    }
    bool TerrainEditing::available(Comet::Entity selected) const {
        return bool(root(selected));
    }
    bool TerrainEditing::select(Comet::Entity selected) {
        auto entity = root(selected);
        if(!entity) {
            cancel();
            m_asset = {};
            return false;
        }
        const auto asset = entity.get_component<Comet::PrefabInstanceComponent>().asset;
        const auto revision = m_assets.database().get_revision(asset);
        if(asset == m_asset && m_root == entity.get_uuid() && m_generation == m_history.generation()
            && revision == m_revision)
            return true;
        cancel();
        auto saved = m_assets.read_terrain({asset, revision});
        const auto* record = m_assets.database().find(asset);
        auto loaded = Comet::TerrainImporter::load_heightfield(
            m_assets.database().paths().assets() / record->path, m_assets.import_limits());
        if(!saved || !loaded) {
            m_error = !saved ? saved.error() : loaded.error();
            m_asset = {};
            return false;
        }
        m_saved = std::move(saved).value();
        m_data = std::move(loaded).value();
        m_asset = asset;
        m_revision = revision;
        m_root = entity.get_uuid();
        m_generation = m_history.generation();
        m_error.clear();
        return true;
    }
    void TerrainEditing::update(const Comet::Ray& world_ray, bool pressed, bool down, bool released,
        double delta, bool lower) {
        auto* scene = m_history.get_scene();
        auto entity = scene ? scene->find_entity(m_root) : Comet::Entity{};
        if(!m_asset || !entity || m_generation != m_history.generation()) {
            cancel();
            return;
        }
        const auto matrix = scene->get_world_matrix(entity);
        if(!Comet::Math::positive_trs_scale(matrix)) {
            m_error = "地形笔刷要求正 TRS 变换";
            cancel();
            return;
        }
        const auto inverse = Comet::Math::inverse(matrix);
        const Comet::Ray local{Comet::Math::Vec3(inverse * Comet::Math::Vec4(world_ray.origin, 1)),
            Comet::Math::Vec3(inverse * Comet::Math::Vec4(world_ray.direction, 0)),
            world_ray.max_parameter};
        m_hover = intersect_terrain(m_data, local);
        if(pressed && m_hover) {
            m_before = m_data;
            m_flatten = m_hover->y;
        }
        if(down && m_before && m_hover) {
            auto settings = brush;
            if(lower
                && (settings.mode == TerrainBrush::Mode::Raise
                    || settings.mode == TerrainBrush::Mode::Lower))
                settings.mode = settings.mode == TerrainBrush::Mode::Raise
                                    ? TerrainBrush::Mode::Lower
                                    : TerrainBrush::Mode::Raise;
            static_cast<void>(apply_terrain_brush(
                m_data, {m_hover->x, m_hover->z}, settings, std::min(delta, .1), m_flatten));
        }
        if(released && m_before)
            static_cast<void>(commit());
    }
    bool TerrainEditing::commit() {
        if(!m_before)
            return true;
        if(m_data == *m_before) {
            m_before.reset();
            return true;
        }
        if(!m_history.execute(
               std::make_unique<TerrainStroke>(m_assets, m_components, m_asset, m_saved, m_data))) {
            m_error = "地形保存或实例更新失败；本次笔刷已取消";
            cancel();
            return false;
        }
        m_saved = m_data;
        m_before.reset();
        m_revision = m_assets.database().get_revision(m_asset);
        m_error.clear();
        return true;
    }
    void TerrainEditing::cancel() {
        if(m_before) {
            m_data = std::move(*m_before);
            m_before.reset();
        }
        m_hover.reset();
    }
    void TerrainEditing::append(Comet::Scene& scene, Comet::LineDrawList& lines) const {
        auto entity = scene.find_entity(m_root);
        if(!m_hover || !entity || !m_asset)
            return;
        const auto world = scene.get_world_matrix(entity);
        const auto point = [&](Comet::Math::Vec2 p) {
            return Comet::Math::Vec3(
                world * Comet::Math::Vec4(p.x, sample(m_data, p) + .025f, p.y, 1));
        };
        const Comet::Math::Vec2 center{m_hover->x, m_hover->z};
        for(int i = 0; i < 64; ++i) {
            const float a = i * Comet::Math::PI / 32, b = (i + 1) * Comet::Math::PI / 32;
            static_cast<void>(lines.add_line(
                point(center + Comet::Math::Vec2(std::cos(a), std::sin(a)) * brush.radius),
                point(center + Comet::Math::Vec2(std::cos(b), std::sin(b)) * brush.radius),
                {1, .7f, .1f, 1}));
        }
        if(!m_before)
            return;
        for(int axis = 0; axis < 2; ++axis)
            for(int row = -8; row <= 8; ++row)
                for(int segment = -8; segment < 8; ++segment) {
                    Comet::Math::Vec2 a{float(row), float(segment)},
                        b{float(row), float(segment + 1)};
                    if(axis) {
                        a = {a.y, a.x};
                        b = {b.y, b.x};
                    }
                    static_cast<void>(lines.add_line(point(center + a * brush.radius / 8.f),
                        point(center + b * brush.radius / 8.f), {.2f, 1, .3f, 1}));
                }
    }
}
