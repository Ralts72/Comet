#pragma once
#include "asset/data/terrain_data.h"
#include "core/geometry.h"
#include "scene/command_history.h"

namespace Comet {
    class LineDrawList;
}
namespace CometEditor {
    class EditorAssets;
    struct TerrainBrush {
        enum class Mode { Raise, Lower, Smooth, Flatten, Paint };
        Mode mode = Mode::Raise;
        float radius = 3, strength = 2;
        uint32_t layer = 0;
    };
    // 坐标与半径使用地形局部米数；高度数据须已展开为内联样本。
    bool apply_terrain_brush(Comet::TerrainData&, Comet::Math::Vec2 center, const TerrainBrush&,
        double delta, float flatten_height);
    std::optional<Comet::Math::Vec3> intersect_terrain(
        const Comet::TerrainData&, const Comet::Ray& local_ray);

    class TerrainEditing final {
    public:
        TerrainEditing(EditorAssets&, CommandHistory&, const Comet::ComponentRegistry&);
        bool available(Comet::Entity selected) const;
        bool select(Comet::Entity selected);
        void update(const Comet::Ray&, bool pressed, bool down, bool released, double delta,
            bool lower = false);
        void cancel();
        void append(Comet::Scene&, Comet::LineDrawList&) const;
        TerrainBrush brush;
        const std::string& error() const { return m_error; }
        bool active() const { return m_before.has_value(); }

    private:
        Comet::Entity root(Comet::Entity) const;
        bool commit();
        EditorAssets& m_assets;
        CommandHistory& m_history;
        const Comet::ComponentRegistry& m_components;
        Comet::EntityUuid m_root;
        Comet::AssetHandle m_asset;
        Comet::AssetRevision m_revision = 0;
        uint64_t m_generation = 0;
        Comet::TerrainData m_data, m_saved;
        std::optional<Comet::TerrainData> m_before;
        std::optional<Comet::Math::Vec3> m_hover;
        float m_flatten = 0;
        std::string m_error;
    };
}
