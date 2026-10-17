#pragma once
#include "animation/animation_data.h"
#include <map>

namespace Comet {
    // 每个源骨架一份映射；未列出的同名节点自动匹配，目标骨长和缩放保持不变。
    struct COMET_API AnimationRetarget {
        struct Chain {
            std::string source_root, source_tip;
            std::string target_root, target_tip;
            float weight = 1;
            bool operator==(const Chain&) const = default;
        };
        std::map<std::string, std::string> nodes;
        std::vector<std::string> translations;
        // 0 按对应骨段的长度比中位数推导；正数显式指定网格空间位移比例。
        float translation_scale = 0;
        // 链覆盖节点映射；绑定时按骨段累计长度重采样旋转，保持目标局部平移。
        std::vector<Chain> chains;
        // 绑定参考姿态的局部欧拉角修正（度），不修改共享模型或动作源。
        std::map<std::string, Math::Vec3> source_pose, target_pose;
        [[nodiscard]] Result<void> validate() const;
        bool operator==(const AnimationRetarget&) const = default;
    };
    [[nodiscard]] COMET_API Result<std::map<std::string, std::string>> suggest_bone_mapping(
        const AnimationData& source, const AnimationData& target,
        const std::map<std::string, std::string>& existing = {});
    [[nodiscard]] COMET_API Result<AnimationPose> retarget_reference_pose(
        const AnimationData&, const std::map<std::string, Math::Vec3>& corrections);
    [[nodiscard]] COMET_API Result<AnimationData> retarget_animation(const AnimationData& source,
        const AnimationData& target, const AnimationRetarget& settings);
}
