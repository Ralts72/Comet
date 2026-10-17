#pragma once
#include "animation/animation_retarget.h"
#include "asset/handle.h"
#include "core/geometry.h"
namespace Comet {
    class LineDrawList;
}
namespace CometEditor {
    class EditorAssets;
    // 只读参考骨架预览；不发布场景播放状态或修改共享源数据。
    class RetargetPreview {
    public:
        explicit RetargetPreview(EditorAssets& assets) : m_assets(assets) {}
        void request(
            Comet::AssetHandle source, Comet::AssetHandle target, const Comet::AnimationRetarget&);
        void update();
        bool suggest(Comet::AnimationRetarget&);
        void append(Comet::LineDrawList&, Comet::Math::Vec3 origin) const;
        bool enabled = false;
        Comet::AssetHandle source_mesh, target_mesh;
        const Comet::AnimationData* source() const {
            return source_mesh == m_loaded_source ? m_source.get() : nullptr;
        }
        const Comet::AnimationData* target() const {
            return target_mesh == m_loaded_target ? m_target.get() : nullptr;
        }
        const std::string& error() const { return m_error; }
        std::string source_bone, target_bone;

    private:
        EditorAssets& m_assets;
        Comet::AnimationRetarget m_settings;
        Comet::AssetHandle m_loaded_source, m_loaded_target;
        uint64_t m_source_revision = 0, m_target_revision = 0;
        bool m_dirty = true;
        std::shared_ptr<const Comet::AnimationData> m_source, m_target;
        std::optional<Comet::AnimationPose> m_source_pose, m_target_pose, m_source_rest,
            m_target_rest;
        std::string m_error;
    };
}
