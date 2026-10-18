#include "reflection/registration.h"
#include "scene/settings_registration.h"
#include "scene/scene_settings.h"

namespace Comet {
    const TypeDescriptor& post_process_descriptor() {
        static const auto reflections =
            describe_type<ScreenSpaceReflectionSettings>("reflections")
                .field(COMET_FIELD(ScreenSpaceReflectionSettings, enabled), "启用",
                    {.required = false})
                .field(COMET_FIELD(ScreenSpaceReflectionSettings, strength), "强度",
                    {.required = false})
                .field(COMET_FIELD(ScreenSpaceReflectionSettings, max_distance), "追踪距离",
                    {.required = false})
                .field(COMET_FIELD(ScreenSpaceReflectionSettings, thickness), "命中厚度",
                    {.required = false})
                .field(COMET_FIELD(ScreenSpaceReflectionSettings, max_roughness), "最大粗糙度",
                    {.required = false})
                .build();
        static const auto lens =
            describe_type<LensSettings>("lens")
                .field(COMET_FIELD(LensSettings, depth_of_field), "景深", {.required = false})
                .field(COMET_FIELD(LensSettings, focus_distance), "对焦距离", {.required = false})
                .field(COMET_FIELD(LensSettings, focus_range), "清晰范围", {.required = false})
                .field(COMET_FIELD(LensSettings, aperture), "弥散半径", {.required = false})
                .field(COMET_FIELD(LensSettings, motion_blur), "运动模糊", {.required = false})
                .field(COMET_FIELD(LensSettings, shutter), "快门比例", {.required = false})
                .field(COMET_FIELD(LensSettings, max_blur), "最大拖影", {.required = false})
                .build();
        static const auto trace =
            describe_type<PathTraceSettings>("trace")
                .field(COMET_FIELD(PathTraceSettings, enabled), "启用", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, dynamic), "动态模式", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, denoise), "降噪", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, samples), "样本上限", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, samples_per_frame), "每帧样本",
                    {.required = false})
                .field(COMET_FIELD(PathTraceSettings, bounces), "反弹上限", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, seed), "随机种子", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, max_dimension), "最长边像素",
                    {.required = false})
                .field(
                    COMET_FIELD(PathTraceSettings, history_weight), "历史权重", {.required = false})
                .field(
                    COMET_FIELD(PathTraceSettings, medium_density), "介质密度", {.required = false})
                .field(COMET_FIELD(PathTraceSettings, medium_anisotropy), "各向异性",
                    {.required = false})
                .field(
                    COMET_FIELD(PathTraceSettings, medium_color), "散射反照率", {.required = false})
                .build();
        static const auto schema =
            describe_type<PostProcessSettings>("post_process", "画面效果")
                .method<&PostProcessSettings::uses_lens>("uses_lens")
                .method<&PostProcessSettings::uses_ao>("uses_ao")
                .method<&PostProcessSettings::uses_bloom>("uses_bloom")
                .method<&PostProcessSettings::uses_reflections>("uses_reflections")
                .field(COMET_FIELD(PostProcessSettings, exposure), "曝光")
                .field(COMET_FIELD(PostProcessSettings, bloom_enabled), "泛光")
                .field(COMET_FIELD(PostProcessSettings, bloom_strength), "泛光强度")
                .field(COMET_FIELD(PostProcessSettings, bloom_threshold), "泛光阈值")
                .field(
                    COMET_FIELD(PostProcessSettings, ao_enabled), "环境遮蔽", {.required = false})
                .field(COMET_FIELD(PostProcessSettings, weighted_oit), "加权透明混合",
                    {.required = false})
                .field(COMET_FIELD(PostProcessSettings, ao_radius), "遮蔽半径", {.required = false})
                .field(
                    COMET_FIELD(PostProcessSettings, ao_strength), "遮蔽强度", {.required = false})
                .field("screen_space_reflections",
                    Comet::ReflectedMember<&PostProcessSettings::reflections>{}, "屏幕空间反射",
                    reflections, {.required = false, .omit_default = true})
                .field(COMET_FIELD(PostProcessSettings, lens), "镜头效果", lens,
                    {.required = false, .omit_default = true})
                .field(COMET_FIELD(PostProcessSettings, path_trace), "路径追踪", trace,
                    {.required = false, .omit_default = true})
                .build();
        return schema;
    }
}
