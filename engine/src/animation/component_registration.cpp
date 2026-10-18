#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    void ComponentRegistration::animation(ComponentRegistry& registry) {
        add(registry,
            describe_component<AnimatorComponent>("animator", "Animator")
                .field(COMET_FIELD(AnimatorComponent, enabled), "enabled", {.required = false})
                .field(COMET_FIELD(AnimatorComponent, clip), "clip", {.required = false})
                .field(COMET_FIELD(AnimatorComponent, speed), "speed",
                    {.required = false,
                        .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(AnimatorComponent, loop), "loop", {.required = false})
                .field(COMET_FIELD(AnimatorComponent, transition), "transition",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(AnimatorComponent, controller), "Controller",
                    {.required = false, .asset_type = AssetType::AnimationController})
                .field(
                    COMET_FIELD(AnimatorComponent, parameters), "Parameters", {.required = false})
                .build());

        add(registry, describe_component<RootMotionComponent>("root_motion", "根运动")
                          .field(COMET_FIELD(RootMotionComponent, enabled), "启用")
                          .field(COMET_FIELD(RootMotionComponent, node), "根骨骼（留空自动）")
                          .field(COMET_FIELD(RootMotionComponent, target), "移动目标（留空自身）")
                          .field(COMET_FIELD(RootMotionComponent, horizontal), "水平位移")
                          .field(COMET_FIELD(RootMotionComponent, vertical), "垂直位移")
                          .field(COMET_FIELD(RootMotionComponent, rotation), "根旋转")
                          .build());

        add(registry, describe_component<HandIKComponent>("hand_ik", "手部 IK")
                          .field(COMET_FIELD(HandIKComponent, enabled), "启用")
                          .field(COMET_FIELD(HandIKComponent, left_hand), "左手骨骼")
                          .field(COMET_FIELD(HandIKComponent, right_hand), "右手骨骼")
                          .field(COMET_FIELD(HandIKComponent, left_target), "左手目标")
                          .field(COMET_FIELD(HandIKComponent, right_target), "右手目标")
                          .field(COMET_FIELD(HandIKComponent, left_pole), "左肘提示点（模型空间）")
                          .field(COMET_FIELD(HandIKComponent, right_pole), "右肘提示点（模型空间）")
                          .field(COMET_FIELD(HandIKComponent, weight), "权重",
                              {.numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}})
                          .field(COMET_FIELD(HandIKComponent, match_rotation), "匹配目标朝向")
                          .build());

        add(registry,
            describe_component<FullBodyIKComponent>("full_body_ik", "全身 IK")
                .field(COMET_FIELD(FullBodyIKComponent, enabled), "启用")
                .field(COMET_FIELD(FullBodyIKComponent, root), "骨盆／躯干根骨骼")
                .field(COMET_FIELD(FullBodyIKComponent, left_foot), "左脚骨骼")
                .field(COMET_FIELD(FullBodyIKComponent, right_foot), "右脚骨骼")
                .field(COMET_FIELD(FullBodyIKComponent, left_target), "左脚目标（空时沿用贴地）")
                .field(COMET_FIELD(FullBodyIKComponent, right_target), "右脚目标（空时沿用贴地）")
                .field(COMET_FIELD(FullBodyIKComponent, left_pole), "左膝提示点")
                .field(COMET_FIELD(FullBodyIKComponent, right_pole), "右膝提示点")
                .field(COMET_FIELD(FullBodyIKComponent, weight), "身体跟随权重",
                    {.numeric = {.minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(FullBodyIKComponent, max_translation), "骨盆最大位移（米）",
                    {.numeric = {.minimum = 0, .maximum = 2, .enforce_bounds = true}})
                .field(COMET_FIELD(FullBodyIKComponent, max_rotation), "躯干最大转角（度）",
                    {.numeric = {.minimum = 0, .maximum = 60, .enforce_bounds = true}})
                .field(COMET_FIELD(FullBodyIKComponent, iterations), "求解迭代数",
                    {.numeric = {.minimum = 1, .maximum = 12, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<MotionWarpComponent>("motion_warp", "动作对位")
                .field(COMET_FIELD(MotionWarpComponent, enabled), "启用")
                .field(COMET_FIELD(MotionWarpComponent, target), "对位目标")
                .field(COMET_FIELD(MotionWarpComponent, state), "状态／片段（留空匹配全部）")
                .field(COMET_FIELD(MotionWarpComponent, start), "窗口起点（0～1）",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(MotionWarpComponent, end), "窗口终点（0～1）",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(MotionWarpComponent, max_distance), "最大轨迹修正 (m)",
                    {.numeric = {.speed = .1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(MotionWarpComponent, max_angle), "最大朝向修正 (°)",
                    {.numeric = {.speed = 1, .minimum = 0, .maximum = 180, .enforce_bounds = true}})
                .field(COMET_FIELD(MotionWarpComponent, weight), "权重",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<FootPlacementComponent>("foot_placement", "脚部 IK")
                .field(COMET_FIELD(FootPlacementComponent, enabled), "启用")
                .field(COMET_FIELD(FootPlacementComponent, character), "角色（留空使用父实体）")
                .field(COMET_FIELD(FootPlacementComponent, left_foot), "左脚骨骼")
                .field(COMET_FIELD(FootPlacementComponent, right_foot), "右脚骨骼")
                .field(COMET_FIELD(FootPlacementComponent, weight), "权重",
                    {.numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, foot_height), "脚底到踝关节 (m)",
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, max_offset), "最大高度修正 (m)",
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, plant_height), "抬脚解除高度 (m)",
                    {.numeric = {.speed = .01f, .minimum = .001f, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, probe_up), "向上探测距离 (m)",
                    {.numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, probe_down), "向下探测距离 (m)",
                    {.numeric = {.speed = .01f, .minimum = .001f, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, max_slope), "最大坡度 (°)",
                    {.numeric = {.speed = 1, .minimum = 0, .maximum = 89, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, response), "响应速度",
                    {.numeric = {.speed = .5f, .minimum = .01f, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, knee_forward), "膝盖朝向（模型空间）")
                .field(COMET_FIELD(FootPlacementComponent, pelvis), "骨盆骨骼", {.required = false})
                .field(COMET_FIELD(FootPlacementComponent, pelvis_weight), "骨盆补偿权重",
                    {.required = false,
                        .numeric =
                            {.speed = .01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(FootPlacementComponent, max_pelvis_offset), "骨盆最大下降 (m)",
                    {.required = false,
                        .numeric = {.speed = .01f, .minimum = 0, .enforce_bounds = true}})
                .build());

        add(registry, describe_component<CharacterAnimationComponent>(
                          "character_animation", "Character Animation")
                          .field(COMET_FIELD(CharacterAnimationComponent, character), "Character")
                          .build());

        add(registry, describe_component<BoneAttachmentComponent>("bone_attachment", "骨骼挂点")
                          .field(COMET_FIELD(BoneAttachmentComponent, bone), "骨骼")
                          .build());
    }
    void ComponentChangeAccess::attachment(Scene& scene, Entity entity, bool) {
        scene.mark_transform_dirty(entity.m_handle);
    }

}
