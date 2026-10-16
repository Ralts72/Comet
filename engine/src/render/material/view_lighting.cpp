#include "render/material/view_lighting.h"

#include "core/geometry.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/resource/environment.h"
#include "render/resource/irradiance.h"
#include "render/resource/texture.h"

#include <cmath>

namespace Comet {
    ViewLightingData ViewLightingData::prepare(
        const RenderSubmission& submission, const LightingData& lighting) {
        ViewLightingData selected{.lighting = lighting};
        if(!submission.view_project_matrix)
            return selected;
        const auto& settings = submission.world.environment;
        if(settings.lighting && submission.world.environment_resource
            && submission.world.environment_resource->has_lighting()) {
            selected.environment = submission.world.environment_resource;
            const float rotation = Math::radians(settings.rotation);
            const auto info =
                selected.environment->specular->get_image_view()->get_image()->get_info();
            selected.lighting.environment = {settings.lighting_intensity,
                float(info.mip_levels - 1), std::sin(rotation), std::cos(rotation)};
        }
        const auto& view = *submission.view_project_matrix;
        const auto frustum = Frustum::from_view_projection(view.projection * view.view);
        for(const auto& resolved : submission.world.reflection_probes) {
            const auto& probe = resolved.probe;
            if(!resolved.environment || !resolved.environment->has_lighting()
                || !probe.bounds.is_valid()
                || glm::any(glm::lessThanEqual(probe.bounds.size(), Math::Vec3(0)))
                || !Math::is_finite(probe.position) || !std::isfinite(probe.intensity)
                || probe.intensity <= 0 || probe.intensity > SceneEnvironment::MAX_INTENSITY
                || !std::isfinite(probe.rotation) || !std::isfinite(probe.blend_distance)
                || probe.blend_distance < 0 || (frustum && !frustum->intersects(probe.bounds)))
                continue;
            if(selected.probe_count == LightingData::MAX_REFLECTION_PROBES) {
                ++selected.excess_probes;
                continue;
            }
            const auto index = selected.probe_count++;
            selected.probes[index] = resolved.environment;
            const auto info =
                resolved.environment->specular->get_image_view()->get_image()->get_info();
            const float rotation = Math::radians(probe.rotation);
            selected.lighting.reflection_probes[index] = {{probe.position, probe.intensity},
                {probe.bounds.minimum, probe.blend_distance},
                {probe.bounds.maximum, float(info.mip_levels - 1)},
                {std::sin(rotation), std::cos(rotation), probe.box_projection ? 1.0f : 0.0f, 0}};
            if(!selected.environment)
                selected.environment = resolved.environment;
        }
        selected.lighting.reflection_metadata = {
            float(selected.probe_count), float(selected.excess_probes), 0, 0};
        for(const auto& resolved : submission.world.irradiance_volumes) {
            const auto& volume = resolved.volume;
            const auto& resource = resolved.resource;
            if(!resource || !resource->probes || !std::isfinite(volume.intensity)
                || volume.intensity <= 0 || volume.intensity > SceneEnvironment::MAX_INTENSITY
                || !std::isfinite(volume.normal_bias) || volume.normal_bias < 0
                || volume.normal_bias > 1 || (frustum && !frustum->intersects(resource->bounds())))
                continue;
            if(selected.irradiance) {
                ++selected.excess_irradiance_volumes;
                continue;
            }
            selected.irradiance = resource;
            selected.lighting.irradiance_origin = {resource->origin, volume.intensity};
            selected.lighting.irradiance_spacing_bias = {resource->spacing, volume.normal_bias};
            selected.lighting.irradiance_dimensions = {Math::Vec3(resource->dimensions), 0};
            selected.irradiance_probes =
                resource->dimensions.x * resource->dimensions.y * resource->dimensions.z;
            selected.irradiance_bytes = uint64_t(selected.irradiance_probes)
                                        * Irradiance::TEXELS_PER_PROBE * sizeof(Math::Vec4);
        }
        return selected;
    }
}
