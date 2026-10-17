#include "animation/retarget_preview.h"
#include "assets/editor_assets.h"
#include "render/debug/line_draw_list.h"
#include <algorithm>
namespace CometEditor {
    void RetargetPreview::request(Comet::AssetHandle source, Comet::AssetHandle target,
        const Comet::AnimationRetarget& settings) {
        if(source_mesh != source)
            source_bone.clear();
        if(target_mesh != target)
            target_bone.clear();
        source_mesh = source;
        target_mesh = target;
        // 映射、位移比例与骨骼链不改变这两份参考姿态。
        if(m_settings.source_pose != settings.source_pose
            || m_settings.target_pose != settings.target_pose) {
            m_settings.source_pose = settings.source_pose;
            m_settings.target_pose = settings.target_pose;
            m_dirty = true;
        }
    }
    void RetargetPreview::update() {
        if(!enabled)
            return;
        const auto load = [&](Comet::AssetHandle mesh, Comet::AssetHandle& loaded,
                              uint64_t& revision,
                              std::shared_ptr<const Comet::AnimationData>& output,
                              std::optional<Comet::AnimationPose>& rest) {
            const auto current = m_assets.mesh_data_revision(mesh);
            if(loaded == mesh && revision == current)
                return bool(output);
            loaded = mesh;
            revision = current;
            output.reset();
            rest.reset();
            auto data = m_assets.read_mesh_data(mesh);
            if(!data || !data.value().animation) {
                m_error = data ? "所选网格没有骨架" : data.error().message;
                return false;
            }
            auto pose = Comet::retarget_reference_pose(*data.value().animation, {});
            if(!pose) {
                m_error = pose.error();
                return false;
            }
            output = data.value().animation;
            rest = std::move(pose).value();
            m_dirty = true;
            return true;
        };
        if(!load(source_mesh, m_loaded_source, m_source_revision, m_source, m_source_rest)
            || !load(target_mesh, m_loaded_target, m_target_revision, m_target, m_target_rest))
            return;
        if(!m_dirty)
            return;
        m_dirty = false;
        m_error.clear();
        auto source = Comet::retarget_reference_pose(*m_source, m_settings.source_pose);
        auto target = Comet::retarget_reference_pose(*m_target, m_settings.target_pose);
        if(!source || !target) {
            m_error = !source ? source.error() : target.error();
            m_source_pose.reset();
            m_target_pose.reset();
            return;
        }
        m_source_pose = std::move(source).value();
        m_target_pose = std::move(target).value();
    }
    bool RetargetPreview::suggest(Comet::AnimationRetarget& settings) {
        if(!source() || !target())
            return false;
        auto matched = Comet::suggest_bone_mapping(*m_source, *m_target, settings.nodes);
        if(!matched) {
            m_error = matched.error();
            return false;
        }
        settings.nodes = std::move(matched).value();
        return true;
    }
    void RetargetPreview::append(Comet::LineDrawList& lines, Comet::Math::Vec3 origin) const {
        using namespace Comet;
        if(!enabled || !source() || !target() || !m_error.empty() || !m_source_pose
            || !m_target_pose)
            return;
        const auto draw = [&](const AnimationData& data, const AnimationPose& pose,
                              const AnimationPose& rest, float side, Math::Vec4 color) {
            const float scale = 2.f / std::max(rest.bounds.size().y, .01f);
            const Math::Vec3 offset = origin + Math::Vec3(side, -rest.bounds.minimum.y * scale, 0);
            const auto skeleton = [&](const AnimationPose& frame, Math::Vec4 tint) {
                for(size_t i = 0; i < data.joints.size(); ++i) {
                    const auto node = data.joints[i].node;
                    const auto parent =
                        std::ranges::find(data.joints, data.nodes[node].parent, &SkinJoint::node);
                    if(parent == data.joints.end())
                        continue;
                    const auto point = [&](size_t index) {
                        return offset + Math::Vec3(frame.bone_matrices[index][3]) * scale;
                    };
                    static_cast<void>(lines.add_line(
                        point(i), point(size_t(parent - data.joints.begin())), tint));
                }
            };
            skeleton(rest, {.4f, .4f, .4f, 1});
            skeleton(pose, color);
        };
        draw(*m_source, *m_source_pose, *m_source_rest, -1.5f, {.1f, 1, 1, 1});
        draw(*m_target, *m_target_pose, *m_target_rest, 1.5f, {1, .6f, .1f, 1});
    }
}
