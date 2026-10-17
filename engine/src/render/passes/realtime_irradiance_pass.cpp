#include "render/passes/realtime_irradiance_pass.h"

#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/gpu_timer.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/scene/render_submission.h"
#include "render/render_graph.h"
#include "render/resource/irradiance.h"
#include "render/resource/texture.h"
#include "render/resource/ray_scene_buffers.h"
#include "asset/data/irradiance_data.h"
#include "realtime_irradiance_comp.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        struct Parameters {
            Math::Vec4 origin_distance;
            Math::Vec4 spacing_history;
            Math::Vec4 grid_dimensions;
            Math::Vec4 visibility;
            Math::Vec4 background;
            std::array<uint32_t, 4> dimensions;
            std::array<uint32_t, 4> counts;
            Math::Vec4 environment;
        };
        static_assert(sizeof(Parameters) == 128);

        bool same_grid(const RenderIrradianceVolume& a, const RenderIrradianceVolume& b) {
            return a.entity_id == b.entity_id && a.center == b.center
                   && a.half_extents == b.half_extents && a.spacing == b.spacing
                   && a.max_distance == b.max_distance && a.bounce_strength == b.bounce_strength;
        }
    }

    struct RealtimeIrradiancePass::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors, feedback;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
        std::shared_ptr<Sampler> sampler;
    };
    struct RealtimeIrradiancePass::History {
        std::array<std::shared_ptr<Irradiance>, 2> volumes;
        RenderIrradianceVolume region;
        uint32_t output = 0;
        uint32_t cursor = 0;
        uint32_t iteration = 0;
        uint32_t evaluated_probes = 0;
        uint32_t visibility_refresh_remaining = 0;
        bool initialized = false;

        void clear(CommandBuffer& command) const {
            const vk::ImageSubresourceRange color(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            for(const auto& image : volumes)
                command.get().clearColorImage(image->probes->get_image_view()->get_image()->get(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0}), color);
        }

        void copy(CommandBuffer& command) const {
            const vk::ImageSubresourceLayers layer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            const vk::ImageCopy region(layer, {}, layer, {},
                {Irradiance::TEXELS_PER_PROBE, volumes[output]->probe_count(), 1});
            command.get().copyImage(volumes[output]->probes->get_image_view()->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal,
                volumes[1 - output]->probes->get_image_view()->get_image()->get(),
                vk::ImageLayout::eTransferDstOptimal, region);
        }
    };
    struct RealtimeIrradiancePass::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<History> history;
        std::shared_ptr<RaySceneBuffers> buffers;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        Result<void, GraphicsError> bind(Device& device);
        void trace(CommandBuffer& command, const RenderIrradianceVolume& volume, uint32_t grant,
            const Feedback&) const;
    };

    // 每 GPU 帧槽只改写自身描述符；读到的全是本轮更新前的区域历史。
    struct RealtimeIrradiancePass::Feedback {
        std::shared_ptr<Program> program;
        std::shared_ptr<CPUBuffer> parameters;
        std::array<std::shared_ptr<Irradiance>, LightingData::MAX_IRRADIANCE_VOLUMES> volumes;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
    };

    Result<std::shared_ptr<RealtimeIrradiancePass::Feedback>, GraphicsError>
    RealtimeIrradiancePass::prepare_feedback(
        FrameScheduler& frames, std::span<const RenderIrradianceVolume> volumes) {
        using Prepared = Result<std::shared_ptr<Feedback>, GraphicsError>;
        struct alignas(16) Parameters {
            std::array<LightingData::IrradianceVolume, LightingData::MAX_IRRADIANCE_VOLUMES>
                grids{};
            std::array<uint32_t, 4> counts{};
        } parameters;
        static_assert(sizeof(Parameters) == 208);
        m_feedback.resize(frames.get_frame_slot_count());
        auto& slot = m_feedback.at(frames.get_current_frame_slot_index());
        if(!slot) {
            auto next = std::make_shared<Feedback>();
            next->program = m_program;
            auto buffer =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(parameters), true, nullptr, "GI feedback regions");
            if(!buffer)
                return Prepared::failure(buffer.error());
            next->parameters = std::move(buffer).value();
            DescriptorPoolSizes sizes;
            sizes.add_pool_size(DescriptorType::UniformBuffer, 1);
            sizes.add_pool_size(DescriptorType::SampledImage, LightingData::MAX_IRRADIANCE_VOLUMES);
            sizes.add_pool_size(DescriptorType::Sampler, 1);
            auto pool = DescriptorPool::create(m_device, 1, sizes);
            if(!pool)
                return Prepared::failure(pool.error());
            next->pool = std::move(pool).value();
            auto sets = next->pool->allocate_descriptor_set(*m_program->feedback, 1);
            if(!sets)
                return Prepared::failure(sets.error());
            next->descriptors = std::move(sets).value();
            slot = std::move(next);
        }
        std::array<vk::DescriptorImageInfo, LightingData::MAX_IRRADIANCE_VOLUMES> images;
        for(size_t i = 0; i < images.size(); ++i) {
            const auto index = std::min(i, volumes.size() - 1);
            const auto& history = *m_regions[index]->history;
            slot->volumes[i] = history.volumes[history.output];
            const auto& grid = *slot->volumes[i];
            if(i < volumes.size())
                parameters.grids[i] = {{grid.origin, 1}, {grid.spacing, volumes[i].normal_bias},
                    {Math::Vec3(grid.dimensions), volumes[i].blend_distance}};
            images[i] = vk::DescriptorImageInfo(
                {}, grid.probes->get_image_view()->get(), vk::ImageLayout::eGeneral);
        }
        parameters.counts[0] = uint32_t(volumes.size());
        slot->parameters->write(&parameters);
        const auto set = slot->descriptors.front().get();
        const vk::DescriptorBufferInfo buffer(slot->parameters->get(), 0, sizeof(parameters));
        const vk::DescriptorImageInfo sampler(m_program->sampler->get(), {}, {});
        const std::array writes{vk::WriteDescriptorSet(set, 0, 0, 1,
                                    vk::DescriptorType::eUniformBuffer, nullptr, &buffer),
            vk::WriteDescriptorSet(set, 1, 0, uint32_t(images.size()),
                vk::DescriptorType::eSampledImage, images.data()),
            vk::WriteDescriptorSet(set, 2, 0, 1, vk::DescriptorType::eSampler, &sampler)};
        m_device.get().updateDescriptorSets(writes, {});
        frames.retain_current_frame_resource(slot);
        return Prepared::success(slot);
    }

    struct RealtimeIrradiancePass::Timing {
        std::shared_ptr<GpuTimer> timer;
        GpuTimer::Capabilities capabilities;
        uint64_t serial = 0;
    };

    Result<std::shared_ptr<RealtimeIrradiancePass::Timing>, GraphicsError> RealtimeIrradiancePass::
        prepare_timing(FrameScheduler& frames) {
        using Prepared = Result<std::shared_ptr<Timing>, GraphicsError>;
        for(const auto& slot : m_timings) {
            if(!slot || !slot->serial || !frames.is_frame_serial_complete(slot->serial))
                continue;
            std::array<GpuTimer::Sample, 2> samples;
            auto read = slot->timer->read(samples);
            if(!read)
                return Prepared::failure(read.error());
            if(!read.value())
                continue;
            if(const auto duration =
                    GpuTimer::elapsed_ms(samples[0].ticks, samples[1].ticks, slot->capabilities);
                duration && (!m_statistics.gpu || slot->serial > m_statistics.gpu->serial))
                m_statistics.gpu = GpuTiming{slot->serial, *duration};
            slot->serial = 0;
        }
        m_timings.resize(frames.get_frame_slot_count());
        auto& slot = m_timings[frames.get_current_frame_slot_index()];
        if(!slot) {
            const auto capabilities =
                GpuTimer::capabilities(m_device, frames.get_queue_family_index());
            if(!GpuTimer::elapsed_ms(0, 0, capabilities))
                return Prepared::success(nullptr);
            auto created = GpuTimer::create(m_device, 2);
            if(!created)
                return Prepared::failure(created.error());
            slot = std::make_shared<Timing>(Timing{std::move(created).value(), capabilities});
        }
        return Prepared::success(slot);
    }

    RealtimeIrradiancePass::RealtimeIrradiancePass(Device& device) : m_device(device) {}
    RealtimeIrradiancePass::~RealtimeIrradiancePass() = default;

    Result<void, GraphicsError> RealtimeIrradiancePass::check_support(const Device& device) {
        const auto& capability = device.get_capability();
        const auto limits = capability.physical_device.getProperties().limits;
        if(limits.maxPerStageDescriptorStorageBuffers < 8
            || limits.maxDescriptorSetStorageBuffers < 8
            || limits.maxPerStageDescriptorStorageImages < 2
            || limits.maxPerStageDescriptorSampledImages < LightingData::MAX_IRRADIANCE_VOLUMES
            || limits.maxDescriptorSetSampledImages < LightingData::MAX_IRRADIANCE_VOLUMES
            || limits.maxPerStageDescriptorUniformBuffers < 1 || limits.maxBoundDescriptorSets < 2
            || limits.maxPushConstantsSize < sizeof(Parameters)
            || limits.maxComputeWorkGroupInvocations < 64 || limits.maxComputeWorkGroupSize[0] < 64)
            return Result<void, GraphicsError>::failure(
                {"设备不支持实时探针 GI 的 compute 资源预算"});
        const auto features =
            capability.physical_device.getFormatProperties(vk::Format::eR32G32B32A32Sfloat)
                .optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eStorageImage)
            || !(features & vk::FormatFeatureFlagBits::eSampledImage)
            || !(capability.physical_device.getQueueFamilyProperties()
                     .at(*capability.graphics_queue_family.queue_family_index)
                     .queueFlags
                 & vk::QueueFlagBits::eCompute))
            return Result<void, GraphicsError>::failure({"设备不支持实时探针 GI 的 RGBA32F 图像"});
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> RealtimeIrradiancePass::initialize() {
        if(m_program)
            return Result<void, GraphicsError>::success();
        if(auto support = check_support(m_device); !support)
            return support;
        auto program = std::make_shared<Program>();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t i = 0; i < 10; ++i)
            bindings.add_binding(i,
                i == 3 || i == 9 ? DescriptorType::StorageImage : DescriptorType::StorageBuffer,
                Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(m_device, bindings);
        if(!descriptors)
            return Result<void, GraphicsError>::failure(descriptors.error());
        program->descriptors = std::move(descriptors).value();
        DescriptorSetLayoutBindings feedback_bindings;
        feedback_bindings.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        feedback_bindings.add_binding(1, DescriptorType::SampledImage,
            Flags<ShaderStage>(ShaderStage::Compute), LightingData::MAX_IRRADIANCE_VOLUMES);
        feedback_bindings.add_binding(
            2, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Compute));
        auto feedback = DescriptorSetLayout::create(m_device, feedback_bindings);
        if(!feedback)
            return Result<void, GraphicsError>::failure(feedback.error());
        program->feedback = std::move(feedback).value();
        const std::array sets{program->descriptors->get(), program->feedback->get()};
        const vk::PushConstantRange constants(
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(Parameters));
        const vk::PipelineLayoutCreateInfo layout_info(
            {}, uint32_t(sets.size()), sets.data(), 1, &constants);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            m_device.get(), "Create realtime GI layout", [&](auto* output) noexcept {
                return m_device.get().createPipelineLayout(&layout_info, nullptr, output);
            });
        if(!layout)
            return Result<void, GraphicsError>::failure(layout.error());
        program->layout = std::move(layout).value();
        auto shader = Shader::create(m_device, "realtime probe GI", REALTIME_IRRADIANCE_COMP);
        if(!shader)
            return Result<void, GraphicsError>::failure(shader.error());
        const vk::PipelineShaderStageCreateInfo stage(
            {}, vk::ShaderStageFlagBits::eCompute, shader.value()->get(), "main");
        const vk::ComputePipelineCreateInfo info({}, stage, program->layout.get());
        auto pipeline = Graphics::create_handle<vk::Pipeline>(
            m_device.get(), "Create realtime GI pipeline", [&](auto* output) noexcept {
                return m_device.get().createComputePipelines({}, 1, &info, nullptr, output);
            });
        if(!pipeline)
            return Result<void, GraphicsError>::failure(pipeline.error());
        program->pipeline = std::move(pipeline).value();
        const SamplerDesc sampler{.mag_filter = Filter::Nearest,
            .min_filter = Filter::Nearest,
            .mipmap_mode = SamplerMipmapMode::Nearest,
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge};
        auto sampled = Sampler::create(m_device, sampler);
        if(!sampled)
            return Result<void, GraphicsError>::failure(sampled.error());
        program->sampler = std::move(sampled).value();
        m_program = std::move(program);
        return Result<void, GraphicsError>::success();
    }

    Result<Math::Vec3u> RealtimeIrradiancePass::validate_region(
        const RenderIrradianceVolume& volume) {
        if(!Math::is_finite(volume.center) || !Math::is_finite(volume.half_extents)
            || Math::any_less_equal(volume.half_extents, Math::Vec3(0))
            || !std::isfinite(volume.spacing) || volume.spacing < .01f
            || !std::isfinite(volume.max_distance) || volume.max_distance <= 0
            || volume.max_distance > 100000 || !volume.update_budget || volume.update_budget > 4096
            || volume.update_rays < 64 || volume.update_rays > 256
            || !std::isfinite(volume.history_weight) || volume.history_weight < 0
            || volume.history_weight > .99f || !std::isfinite(volume.normal_bias)
            || volume.normal_bias < 0 || !std::isfinite(volume.bounce_strength)
            || volume.bounce_strength < 0 || volume.bounce_strength > 1)
            return Result<Math::Vec3u>::failure("实时探针 GI 的区域或更新预算无效");
        const auto cells =
            Math::max(Math::ceil(volume.half_extents * 2.0f / volume.spacing), Math::Vec3(2));
        if(Math::any_greater(cells, Math::Vec3(64))
            || double(cells.x) * cells.y * cells.z > IrradianceData::MAX_PROBES)
            return Result<Math::Vec3u>::failure(
                "实时探针 GI 超过 4096 探针预算，请增大间距或缩小区域");
        return Result<Math::Vec3u>::success(Math::Vec3u(cells));
    }

    Result<std::shared_ptr<RealtimeIrradiancePass::History>, GraphicsError> RealtimeIrradiancePass::
        create_history(const RenderIrradianceVolume& volume, Math::Vec3u dimensions) {
        using Created = Result<std::shared_ptr<History>, GraphicsError>;
        auto history = std::make_shared<History>();
        history->region = volume;
        const uint32_t count = dimensions.x * dimensions.y * dimensions.z;
        const auto limits = m_device.get_capability().physical_device.getProperties().limits;
        if(count > limits.maxImageDimension2D)
            return Created::failure({"探针纹理超过设备尺寸限制"});
        const auto spacing = volume.half_extents * 2.0f / Math::Vec3(dimensions);
        for(auto& output : history->volumes) {
            auto image = Image::try_create(m_device,
                {Format::R32G32B32A32_SFLOAT, {Irradiance::TEXELS_PER_PROBE, count, 1},
                    Flags<ImageUsage>(ImageUsage::StorageBinding) | ImageUsage::Sampled
                        | ImageUsage::CopySrc | ImageUsage::CopyDst},
                false, SampleCount::Count1, "realtime GI history");
            if(!image)
                return Created::failure({"Cannot allocate realtime GI history", image.result()});
            auto view = ImageView::try_create(
                m_device, std::move(image).value(), Flags<ImageAspect>(ImageAspect::Color));
            if(!view)
                return Created::failure({"Cannot create realtime GI view", view.result()});
            output = std::make_shared<Irradiance>();
            output->origin = volume.center - volume.half_extents + spacing * .5f;
            output->spacing = spacing;
            output->dimensions = dimensions;
            output->probes = Texture::from_image(std::move(view).value(), m_program->sampler);
        }
        return Created::success(std::move(history));
    }

    Result<void, GraphicsError> RealtimeIrradiancePass::Resources::bind(Device& device) {
        using Bound = Result<void, GraphicsError>;
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::StorageBuffer, 16);
        sizes.add_pool_size(DescriptorType::StorageImage, 4);
        auto created_pool = DescriptorPool::create(device, 2, sizes);
        if(!created_pool)
            return Bound::failure(created_pool.error());
        pool = std::move(created_pool).value();
        auto sets = pool->allocate_descriptor_set(*program->descriptors, 2);
        if(!sets)
            return Bound::failure(sets.error());
        descriptors = std::move(sets).value();
        for(uint32_t output = 0; output < 2; ++output) {
            buffers->write(device, descriptors[output]);
            const auto& images = history->volumes;
            const std::array infos{
                vk::DescriptorImageInfo(
                    {}, images[output]->probes->get_image_view()->get(), vk::ImageLayout::eGeneral),
                vk::DescriptorImageInfo({}, images[1 - output]->probes->get_image_view()->get(),
                    vk::ImageLayout::eGeneral)};
            const std::array writes{vk::WriteDescriptorSet(descriptors[output].get(), 3, 0, 1,
                                        vk::DescriptorType::eStorageImage, &infos[0]),
                vk::WriteDescriptorSet(descriptors[output].get(), 9, 0, 1,
                    vk::DescriptorType::eStorageImage, &infos[1])};
            device.get().updateDescriptorSets(writes, {});
        }
        return Bound::success();
    }

    void RealtimeIrradiancePass::Resources::trace(CommandBuffer& command,
        const RenderIrradianceVolume& volume, uint32_t grant, const Feedback& feedback) const {
        const auto& scene = buffers->source;
        const auto output = 1 - history->output;
        const auto& result = history->volumes[output];
        const bool refresh = history->visibility_refresh_remaining > 0;
        const Parameters parameters{{result->origin, volume.max_distance},
            {result->spacing,
                refresh ? std::min(volume.history_weight, .5f) : volume.history_weight},
            {Math::Vec3(result->dimensions), 0},
            {2.f * std::max({result->spacing.x, result->spacing.y, result->spacing.z}),
                refresh ? 0.f : volume.history_weight, volume.bounce_strength, volume.normal_bias},
            Math::Vec4(scene->background, 1),
            {history->cursor, grant, volume.update_rays, history->iteration},
            {uint32_t(scene->geometry->nodes.size()), uint32_t(scene->lighting->light_count), 0,
                uint32_t(scene->emitters->size())},
            scene->environment};
        const std::array sets{descriptors[output].get(), feedback.descriptors.front().get()};
        command.get().bindDescriptorSets(vk::PipelineBindPoint::eCompute, program->layout.get(), 0,
            uint32_t(sets.size()), sets.data(), 0, nullptr);
        command.get().pushConstants(program->layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
            sizeof(parameters), &parameters);
        command.get().dispatch(grant, 1, 1);
    }

    Result<void, GraphicsError> RealtimeIrradiancePass::prepare(uint64_t scene_lifetime,
        const std::shared_ptr<const RayScene>& scene,
        std::span<const RenderIrradianceVolume> volumes) {
        using Prepared = Result<void, GraphicsError>;
        if(!scene || !scene->geometry || volumes.empty()
            || volumes.size() > LightingData::MAX_IRRADIANCE_VOLUMES)
            return Prepared::failure({"实时 GI 需要有效场景及 1～4 个区域"});
        std::array<Math::Vec3u, LightingData::MAX_IRRADIANCE_VOLUMES> dimensions;
        for(std::size_t i = 0; i < volumes.size(); ++i) {
            const auto valid = validate_region(volumes[i]);
            if(!valid)
                return Prepared::failure({valid.error()});
            dimensions[i] = valid.value();
            for(std::size_t j = 0; j < i; ++j)
                if(volumes[i].entity_id == volumes[j].entity_id)
                    return Prepared::failure({"实时 GI 区域身份重复"});
        }
        if(auto initialized = initialize(); !initialized)
            return initialized;
        const auto* prior = m_buffers ? m_buffers->source.get() : nullptr;
        const bool compatible = prior && m_scene_lifetime == scene_lifetime
                                && (prior->geometry == scene->geometry
                                    || (prior->geometry->topology
                                        && prior->geometry->topology == scene->geometry->topology))
                                && prior->materials == scene->materials
                                && prior->environment_map == scene->environment_map
                                && prior->environment == scene->environment
                                && prior->background == scene->background;
        const bool same_scene = prior == scene.get();
        if(compatible && same_scene && volumes.size() == m_regions.size()) {
            bool unchanged = true;
            for(std::size_t i = 0; i < volumes.size(); ++i)
                unchanged &= same_grid(m_regions[i]->history->region, volumes[i]);
            if(unchanged)
                return Prepared::success();
        }
        auto buffers = m_buffers;
        if(!same_scene) {
            auto prepared = RaySceneBuffers::prepare(m_device, scene, m_buffers);
            if(!prepared)
                return Prepared::failure(prepared.error());
            buffers = std::move(prepared).value();
        }
        std::vector<std::shared_ptr<Resources>> next;
        next.reserve(volumes.size());
        for(std::size_t i = 0; i < volumes.size(); ++i) {
            std::shared_ptr<Resources> previous;
            if(compatible)
                for(const auto& resource : m_regions)
                    if(same_grid(resource->history->region, volumes[i])) {
                        previous = resource;
                        break;
                    }
            if(previous && previous->buffers == buffers) {
                next.push_back(previous);
                continue;
            }
            auto resource = std::make_shared<Resources>();
            resource->program = m_program;
            resource->buffers = buffers;
            if(previous) {
                resource->history = previous->history;
            } else {
                auto history = create_history(volumes[i], dimensions[i]);
                if(!history)
                    return Prepared::failure(history.error());
                resource->history = std::move(history).value();
            }
            if(auto bound = resource->bind(m_device); !bound)
                return bound;
            next.push_back(std::move(resource));
        }
        if(buffers != m_buffers) {
            m_statistics.upload_bytes += buffers->upload_bytes;
            m_statistics.geometry_upload_bytes += buffers->geometry_upload_bytes;
        }
        m_buffers = std::move(buffers);
        m_regions = std::move(next);
        m_scene_lifetime = scene_lifetime;
        return Prepared::success();
    }

    Result<void, GraphicsError> RealtimeIrradiancePass::record_regions(FrameScheduler& frames,
        std::span<const RenderIrradianceVolume> volumes, std::span<const uint32_t> grants,
        bool geometry_moved, std::span<ResolvedIrradianceVolume> outputs) {
        using Recorded = Result<void, GraphicsError>;
        const auto stages =
            Flags<PipelineStage>(PipelineStage::FragmentShader) | PipelineStage::ComputeShader;
        const auto compute = Flags<PipelineStage>(PipelineStage::ComputeShader);
        const auto range = ImageSubresourceRange{.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        auto feedback = prepare_feedback(frames, volumes);
        if(!feedback)
            return Recorded::failure(feedback.error());
        RenderGraph graph;
        RenderGraph::Pass clear_pass{"initialize GI histories"}, copy_pass{"preserve GI probes"},
            trace_pass{"update GI probes"};
        std::vector<RenderGraph::Binding> bindings;
        bindings.reserve(volumes.size() * 2);
        std::array<std::size_t, LightingData::MAX_IRRADIANCE_VOLUMES> active;
        std::size_t active_count = 0;
        for(std::size_t i = 0; i < volumes.size(); ++i) {
            auto& history = *m_regions[i]->history;
            if(geometry_moved)
                history.visibility_refresh_remaining = history.volumes[0]->probe_count();
            outputs[i] = {volumes[i], history.volumes[history.output], 0, history.cursor, 0,
                history.evaluated_probes};
            if(grants[i] || !history.initialized)
                active[active_count++] = i;
            const auto initial =
                history.initialized
                    ? *resolve_image_state(ResourceUsage::SampledRead, range, stages)
                    : ImageState{.subresources = range};
            const auto source = graph.import_image("GI previous " + std::to_string(i), initial);
            const auto destination = graph.import_image("GI updated " + std::to_string(i), initial);
            bindings.push_back(
                history.volumes[history.output]->probes->get_image_view()->get_image());
            bindings.push_back(
                history.volumes[1 - history.output]->probes->get_image_view()->get_image());
            if(!history.initialized) {
                clear_pass.uses.push_back({source, ResourceUsage::TransferDestination, {}});
                clear_pass.uses.push_back({destination, ResourceUsage::TransferDestination, {}});
            }
            if(grants[i]) {
                copy_pass.uses.push_back({source, ResourceUsage::TransferSource, {}});
                copy_pass.uses.push_back({destination, ResourceUsage::TransferDestination, {}});
                trace_pass.uses.push_back({destination, ResourceUsage::StorageReadWrite, compute});
            }
            // 自身历史 imageLoad 与区域采样共用 GENERAL 布局；未分到预算的区域也可贡献上一帧光照。
            trace_pass.uses.push_back({source, ResourceUsage::StorageRead, compute});
            graph.export_resource({source, ResourceUsage::SampledRead, stages});
            graph.export_resource({destination, ResourceUsage::SampledRead, stages});
        }
        // 全部区域先复制再计算，避免每区域的 transfer/compute 切换串行化独立 dispatch。
        std::optional<RenderGraph::PassId> clear, copy, trace;
        if(!clear_pass.uses.empty())
            clear = graph.add_pass(std::move(clear_pass));
        if(!copy_pass.uses.empty())
            copy = graph.add_pass(std::move(copy_pass));
        if(!trace_pass.uses.empty())
            trace = graph.add_pass(std::move(trace_pass));
        auto plan = graph.compile();
        if(!plan)
            return Recorded::failure({plan.error()});
        const auto active_regions = std::span(active).first(active_count);
        const auto record = [&](RenderGraph::PassId pass, CommandBuffer& command) {
            if(trace && pass == *trace)
                command.get().bindPipeline(
                    vk::PipelineBindPoint::eCompute, m_program->pipeline.get());
            for(const auto i : active_regions) {
                const auto& resources = m_regions[i];
                const auto& history = *resources->history;
                if(clear && pass == *clear && !history.initialized)
                    history.clear(command);
                else if(copy && pass == *copy && grants[i])
                    history.copy(command);
                else if(trace && pass == *trace && grants[i])
                    resources->trace(command, volumes[i], grants[i], *feedback.value());
            }
            return Recorded::success();
        };
        for(const auto i : active_regions)
            frames.retain_current_frame_resource(m_regions[i]);
        if(auto recorded = plan.value().record(frames, bindings, record); !recorded)
            return recorded;
        for(const auto i : active_regions) {
            auto& history = *m_regions[i]->history;
            const auto count = history.volumes[0]->probe_count();
            history.initialized = true;
            // 首圈连续更新；完整求值后无需逐探针 CPU 标记。
            history.evaluated_probes = std::min(count, history.evaluated_probes + grants[i]);
            history.visibility_refresh_remaining -=
                std::min(history.visibility_refresh_remaining, grants[i]);
            history.output = 1 - history.output;
            outputs[i].resource = history.volumes[history.output];
            outputs[i].updated_count = grants[i];
            outputs[i].evaluated_probes = history.evaluated_probes;
            history.cursor = (history.cursor + grants[i]) % count;
            if(grants[i])
                ++history.iteration;
        }
        return Recorded::success();
    }

    Result<std::vector<ResolvedIrradianceVolume>, GraphicsError> RealtimeIrradiancePass::update(
        FrameScheduler& frames, uint64_t scene_lifetime,
        const std::shared_ptr<const RayScene>& scene,
        std::span<const RenderIrradianceVolume> volumes, uint32_t total_budget, bool measure) {
        using Updated = Result<std::vector<ResolvedIrradianceVolume>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device || !total_budget
            || total_budget > 16384)
            return Updated::failure({"Invalid realtime GI frame or total budget"});
        if(m_recorded_serial == frames.get_current_frame_serial())
            reset();
        const bool geometry_moved =
            scene && m_buffers && m_buffers->source->geometry != scene->geometry;
        if(auto ready = prepare(scene_lifetime, scene, volumes); !ready)
            return Updated::failure(ready.error());
        std::array<uint32_t, LightingData::MAX_IRRADIANCE_VOLUMES> grants{}, limits{};
        uint32_t remaining = 0;
        for(std::size_t i = 0; i < volumes.size(); ++i) {
            limits[i] = std::min(
                volumes[i].update_budget, m_regions[i]->history->volumes[0]->probe_count());
            remaining += limits[i];
        }
        remaining = std::min(remaining, total_budget);
        // 持续轮转，预算小于区域数时也不会让后面的区域饥饿。
        m_budget_cursor %= uint32_t(volumes.size());
        while(remaining) {
            const auto index = m_budget_cursor;
            m_budget_cursor = (m_budget_cursor + 1) % uint32_t(volumes.size());
            if(grants[index] < limits[index]) {
                ++grants[index];
                --remaining;
            }
        }
        std::shared_ptr<Timing> timing;
        if(measure) {
            auto prepared = prepare_timing(frames);
            if(!prepared)
                return Updated::failure(prepared.error());
            timing = std::move(prepared).value();
        } else {
            m_statistics.gpu.reset();
        }
        auto& command = frames.get_current_command_buffer();
        if(timing) {
            frames.retain_current_frame_resource(timing);
            timing->timer->reset(command);
            timing->timer->write(command, 0, GpuTimer::Boundary::Begin);
        }
        m_statistics.regions = uint32_t(volumes.size());
        m_statistics.probes = m_statistics.updated_probes = m_statistics.evaluated_probes =
            m_statistics.rays = 0;
        m_statistics.image_bytes = 0;
        std::vector<ResolvedIrradianceVolume> outputs(volumes.size());
        if(auto recorded = record_regions(
               frames, volumes, std::span(grants).first(volumes.size()), geometry_moved, outputs);
            !recorded)
            return Updated::failure(recorded.error());
        for(std::size_t i = 0; i < volumes.size(); ++i) {
            m_statistics.probes += outputs[i].resource->probe_count();
            m_statistics.evaluated_probes += outputs[i].evaluated_probes;
            m_statistics.updated_probes += grants[i];
            m_statistics.rays += grants[i] * (volumes[i].update_rays + 64);
        }
        m_statistics.updates += m_statistics.updated_probes;
        m_statistics.image_bytes =
            uint64_t(m_statistics.probes) * Irradiance::TEXELS_PER_PROBE * sizeof(Math::Vec4) * 2;
        if(timing) {
            timing->timer->write(command, 1, GpuTimer::Boundary::End);
            timing->serial = frames.get_current_frame_serial();
        }
        m_recorded_serial = frames.get_current_frame_serial();
        return Updated::success(std::move(outputs));
    }

    void RealtimeIrradiancePass::reset() {
        m_regions.clear();
        m_buffers.reset();
        m_scene_lifetime = 0;
        m_budget_cursor = 0;
        m_statistics = {};
        m_recorded_serial = 0;
        m_timings.clear();
        m_feedback.clear();
    }
}
