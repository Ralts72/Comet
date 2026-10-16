#include "render/material/view_lighting.h"
#include "asset/data/texture_data.h"
#include "render/passes/volumetric_fog_pass.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"

#include "core/geometry.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/resource/environment.h"
#include "render/resource/irradiance.h"
#include "render/resource/texture.h"

#include <cmath>

namespace Comet {
    // 视图 UBO，与 common/frame.glsl 对齐；不把相机姿态存入材质属性。
    struct alignas(16) ViewFrameData {
        ViewProjectMatrix matrices;
        Math::Vec3 camera_position;
        float orthographic;
        Math::Vec3 view_direction;
        float fog_start = 0;
        Math::Vec4 fog_color{}; // RGB 线性雾色，W 为距离区间倒数；零表示关闭。
    };
    static_assert(sizeof(ViewFrameData) == 176);
    static_assert(offsetof(ViewFrameData, camera_position) == 128);
    static_assert(offsetof(ViewFrameData, orthographic) == 140);
    static_assert(offsetof(ViewFrameData, view_direction) == 144);
    static_assert(offsetof(ViewFrameData, fog_start) == 156);
    static_assert(offsetof(ViewFrameData, fog_color) == 160);

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
            const auto& realtime = submission.realtime_irradiance;
            const auto resource = volume.realtime
                                      ? (realtime && realtime->volume.entity_id == volume.entity_id
                                                ? realtime->resource
                                                : nullptr)
                                      : resolved.resource;
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

    Result<void, GraphicsError> ViewBindings::initialize(Device& device, RenderResources& resources,
        uint32_t frame_slots, const std::shared_ptr<Texture>& fallback) {
        if(!frame_slots || !fallback)
            return Result<void, GraphicsError>::failure(
                {"View bindings require frame slots and a fallback texture"});
        m_device = &device;
        m_fallback = fallback;
        auto black_cube = resources.try_create_texture({.width = 1,
            .height = 1,
            .format = Format::R16G16B16A16_SFLOAT,
            .pixels = std::vector<uint8_t>(6 * 8),
            .cubemap = true});
        if(!black_cube)
            return Result<void, GraphicsError>::failure(black_cube.error());
        m_empty_environment = std::make_shared<Environment>(
            Environment{black_cube.value(), black_cube.value(), black_cube.value(), m_fallback});
        SamplerDesc environment_desc{.address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge};
        if(!device.query_format_support(Format::R16G16B16A16_SFLOAT).linear_filter) {
            environment_desc.mag_filter = Filter::Nearest;
            environment_desc.min_filter = Filter::Nearest;
            environment_desc.mipmap_mode = SamplerMipmapMode::Nearest;
        }
        auto environment_sampler = Sampler::create(device, environment_desc);
        if(!environment_sampler)
            return Result<void, GraphicsError>::failure(environment_sampler.error());
        auto shadow_sampler = resources.get_sampler_manager().get_nearest_clamp();
        if(!shadow_sampler)
            return Result<void, GraphicsError>::failure(shadow_sampler.error());
        DescriptorSetLayoutBindings frame_bindings;
        frame_bindings.add_binding(0, DescriptorType::UniformBuffer,
            Flags<ShaderStage>(ShaderStage::Vertex) | ShaderStage::Fragment | ShaderStage::Compute);
        frame_bindings.add_binding(1, DescriptorType::UniformBuffer,
            Flags<ShaderStage>(ShaderStage::Fragment) | ShaderStage::Compute);
        frame_bindings.add_binding(2, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment) | ShaderStage::Compute);
        for(uint32_t binding = 3; binding <= 12; ++binding)
            frame_bindings.add_binding(binding, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto frame_layout = DescriptorSetLayout::create(device, frame_bindings);
        if(!frame_layout)
            return Result<void, GraphicsError>::failure(frame_layout.error());
        m_layout = std::move(frame_layout).value();
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slots * 2);
        pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler,
            frame_slots * (7 + LightingData::MAX_REFLECTION_PROBES));
        auto pool_result = DescriptorPool::create(device, frame_slots, pool_sizes);
        if(!pool_result)
            return Result<void, GraphicsError>::failure(pool_result.error());
        std::shared_ptr<DescriptorPool> pool = std::move(pool_result).value();
        auto descriptors = pool->allocate_descriptor_set(*m_layout, frame_slots);
        if(!descriptors)
            return Result<void, GraphicsError>::failure(descriptors.error());
        for(uint32_t slot = 0; slot < frame_slots; ++slot) {
            auto frame = std::make_shared<Frame>();
            frame->layout = m_layout;
            frame->pool = pool;
            frame->shadow_sampler = shadow_sampler.value();
            frame->environment_sampler = environment_sampler.value();
            auto buffer =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(ViewFrameData), false, nullptr, "frame camera");
            if(!buffer)
                return Result<void, GraphicsError>::failure(buffer.error());
            frame->buffer = std::move(buffer).value();
            frame->descriptor = descriptors.value()[slot];
            auto lighting =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(LightingData), false, nullptr, "frame lighting");
            if(!lighting)
                return Result<void, GraphicsError>::failure(lighting.error());
            frame->lighting = std::move(lighting).value();
            const std::array writes{
                DescriptorSet::UniformBufferWrite{0, *frame->buffer, sizeof(ViewFrameData)},
                DescriptorSet::UniformBufferWrite{1, *frame->lighting, sizeof(LightingData)}};
            const std::array fallbacks{DescriptorSet::ImageSamplerWrite{11,
                                           *m_fallback->get_image_view(), *frame->shadow_sampler},
                DescriptorSet::ImageSamplerWrite{
                    12, *m_fallback->get_image_view(), *frame->shadow_sampler}};
            frame->descriptor->update(device, writes, fallbacks);
            m_frames.push_back(std::move(frame));
        }
        return Result<void, GraphicsError>::success();
    }

    DescriptorSet ViewBindings::descriptor(uint32_t slot) const {
        return *m_frames.at(slot)->descriptor;
    }

    ViewBindings::Statistics ViewBindings::statistics() const {
        auto result = m_statistics;
        result.frame_set_count = static_cast<uint32_t>(m_frames.size());
        return result;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ViewBindings::prepare(
        FrameScheduler& frames, const RenderSubmission& submission, const LightingData& lighting,
        const std::shared_ptr<ImageView>& shadow_map, const std::shared_ptr<ImageView>& view_depth,
        const FogVolume* fog) {
        using Preparation = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != m_device
            || frames.get_current_frame_slot_index() >= m_frames.size() || !shadow_map)
            return Preparation::failure({"Invalid view frame or shadow input"});
        std::vector<QueueSemaphoreSubmit> waits;
        const auto append_wait = [&](const GpuCompletionPoint& completion) {
            if(completion.is_valid() && !completion.is_complete())
                merge_semaphore_wait(
                    waits, QueueSemaphoreSubmit(
                               completion, Flags<PipelineStage>(PipelineStage::FragmentShader)));
        };
        auto& device = *m_device;
        const auto previous_excess_probes = m_statistics.excess_reflection_probes;
        const auto previous_excess_gi = m_statistics.excess_irradiance_volumes;
        const auto previous_omissions =
            std::pair(m_statistics.excess_lights, m_statistics.invalid_lights);
        m_statistics = {};
        if(!submission.view_project_matrix)
            return Preparation::success(std::move(waits));

        const auto& view = *submission.view_project_matrix;
        const auto& frame = m_frames.at(frames.get_current_frame_slot_index());
        const auto camera_world = Math::inverse(view.view);
        // 引擎标准透视矩阵的该项为 -1，正交为 0；不是任意投影的分类器。
        ViewFrameData camera{view, Math::Vec3(camera_world[3]),
            view.projection[2][3] == 0 ? 1.0f : 0.0f, Math::Vec3(camera_world[2])};
        const auto& settings = submission.world.environment;
        if(settings.fog_enabled) {
            camera.fog_start = settings.fog_start;
            camera.fog_color = {settings.fog_color, 1.0f / (settings.fog_end - settings.fog_start)};
        }
        frame->buffer->write(&camera);
        auto selected = ViewLightingData::prepare(submission, lighting);
        selected.lighting.fog_parameters = fog ? fog->parameters : Math::Vec4(0);
        const auto fog_image = fog ? fog->image : m_fallback->get_image_view();
        if(frame->fog != fog_image) {
            const DescriptorSet::ImageSamplerWrite write{12, *fog_image, *frame->shadow_sampler};
            frame->descriptor->update(device, {}, std::span(&write, 1));
            frame->fog = fog_image;
        }
        const auto& environment = selected.environment ? selected.environment : m_empty_environment;
        const auto& frame_lighting = selected.lighting;
        for(unsigned index = 0; index < LightingData::MAX_REFLECTION_PROBES; ++index) {
            const auto& probe =
                selected.probes[index] ? selected.probes[index] : m_empty_environment;
            if(frame->reflection_probes[index] != probe) {
                const DescriptorSet::ImageSamplerWrite write{
                    7 + index, *probe->specular->get_image_view(), *frame->environment_sampler};
                frame->descriptor->update(device, {}, std::span(&write, 1));
                frame->reflection_probes[index] = probe;
            }
            append_wait(probe->specular->get_ready_completion());
        }
        m_statistics.reflection_probes = selected.probe_count;
        m_statistics.excess_reflection_probes = selected.excess_probes;
        if(selected.excess_probes && previous_excess_probes != selected.excess_probes)
            LOG_WARN("Reflection probes omitted {} excess visible probes (limit {})",
                selected.excess_probes, LightingData::MAX_REFLECTION_PROBES);
        const auto& irradiance = selected.irradiance;
        m_statistics.irradiance_probes = selected.irradiance_probes;
        m_statistics.irradiance_bytes = selected.irradiance_bytes;
        m_statistics.excess_irradiance_volumes = selected.excess_irradiance_volumes;
        if(selected.excess_irradiance_volumes
            && previous_excess_gi != selected.excess_irradiance_volumes)
            LOG_WARN("GI omitted {} excess visible volumes (limit 1)",
                selected.excess_irradiance_volumes);
        if(frame->irradiance != irradiance) {
            const auto& texture = irradiance ? irradiance->probes : m_fallback;
            const DescriptorSet::ImageSamplerWrite write{
                11, *texture->get_image_view(), *frame->shadow_sampler};
            frame->descriptor->update(device, {}, std::span(&write, 1));
            frame->irradiance = irradiance;
        }
        if(irradiance)
            append_wait(irradiance->probes->get_ready_completion());
        frame->lighting->write(&frame_lighting);
        if(frame->environment != environment) {
            const std::array writes{
                DescriptorSet::ImageSamplerWrite{
                    3, *environment->irradiance->get_image_view(), *frame->environment_sampler},
                DescriptorSet::ImageSamplerWrite{
                    4, *environment->specular->get_image_view(), *frame->environment_sampler},
                DescriptorSet::ImageSamplerWrite{
                    5, *environment->brdf->get_image_view(), *frame->environment_sampler}};
            frame->descriptor->update(device, {}, writes);
            frame->environment = environment;
        }
        for(const auto& texture :
            {environment->irradiance, environment->specular, environment->brdf})
            append_wait(texture->get_ready_completion());
        if(frame->shadow_map != shadow_map) {
            const DescriptorSet::ImageSamplerWrite write{2, *shadow_map, *frame->shadow_sampler};
            frame->descriptor->update(device, {}, std::span(&write, 1));
            frame->shadow_map = shadow_map;
        }
        const auto& depth = view_depth ? view_depth : shadow_map;
        if(frame->view_depth != depth) {
            const DescriptorSet::ImageSamplerWrite write{6, *depth, *frame->shadow_sampler};
            frame->descriptor->update(device, {}, std::span(&write, 1));
            frame->view_depth = depth;
        }
        m_statistics.light_count = static_cast<uint32_t>(lighting.light_count);
        m_statistics.excess_lights = static_cast<uint32_t>(lighting.excess_lights);
        m_statistics.invalid_lights = static_cast<uint32_t>(lighting.invalid_lights);
        if((m_statistics.excess_lights || m_statistics.invalid_lights)
            && previous_omissions
                   != std::pair(m_statistics.excess_lights, m_statistics.invalid_lights))
            LOG_WARN("Lighting omitted {} excess and {} invalid lights (limit {})",
                m_statistics.excess_lights, m_statistics.invalid_lights, LightingData::MAX_LIGHTS);
        frames.retain_current_frame_resource(frame);
        return Preparation::success(std::move(waits));
    }

}
