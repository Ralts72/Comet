#include "render/passes/realtime_irradiance_pass.h"

#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/gpu_timer.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
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
                   && a.max_distance == b.max_distance;
        }
    }

    struct RealtimeIrradiancePass::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
        std::shared_ptr<Sampler> sampler;
    };
    struct RealtimeIrradiancePass::History {
        std::array<std::shared_ptr<Irradiance>, 2> volumes;
        uint64_t scene_lifetime = 0;
        RenderIrradianceVolume region;
        uint32_t output = 0;
        uint32_t cursor = 0;
        uint32_t iteration = 0;
        uint32_t evaluated_probes = 0;
        uint32_t visibility_refresh_remaining = 0;
        bool initialized = false;
    };
    struct RealtimeIrradiancePass::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<History> history;
        std::shared_ptr<RaySceneBuffers> buffers;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        Result<void, GraphicsError> bind(Device& device);
    };

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
        const auto set = program->descriptors->get();
        const vk::PushConstantRange constants(
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(Parameters));
        const vk::PipelineLayoutCreateInfo layout_info({}, 1, &set, 1, &constants);
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
            || glm::any(glm::lessThanEqual(volume.half_extents, Math::Vec3(0)))
            || !std::isfinite(volume.spacing) || volume.spacing < .01f
            || !std::isfinite(volume.max_distance) || volume.max_distance <= 0
            || volume.max_distance > 100000 || !volume.update_budget || volume.update_budget > 4096
            || volume.update_rays < 64 || volume.update_rays > 256
            || !std::isfinite(volume.history_weight) || volume.history_weight < 0
            || volume.history_weight > .99f)
            return Result<Math::Vec3u>::failure("实时探针 GI 的区域或更新预算无效");
        const auto cells =
            glm::max(glm::ceil(volume.half_extents * 2.0f / volume.spacing), Math::Vec3(2));
        if(glm::any(glm::greaterThan(cells, Math::Vec3(64)))
            || double(cells.x) * cells.y * cells.z > IrradianceData::MAX_PROBES)
            return Result<Math::Vec3u>::failure(
                "实时探针 GI 超过 4096 探针预算，请增大间距或缩小区域");
        return Result<Math::Vec3u>::success(Math::Vec3u(cells));
    }

    Result<std::shared_ptr<RealtimeIrradiancePass::History>, GraphicsError> RealtimeIrradiancePass::
        create_history(
            uint64_t scene_lifetime, const RenderIrradianceVolume& volume, Math::Vec3u dimensions) {
        using Created = Result<std::shared_ptr<History>, GraphicsError>;
        auto history = std::make_shared<History>();
        history->scene_lifetime = scene_lifetime;
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

    Result<void, GraphicsError> RealtimeIrradiancePass::prepare(uint64_t scene_lifetime,
        const std::shared_ptr<const RayScene>& scene, const RenderIrradianceVolume& volume) {
        using Prepared = Result<void, GraphicsError>;
        if(!scene || !scene->geometry)
            return Prepared::failure({"Invalid realtime GI scene"});
        const auto grid = validate_region(volume);
        if(!grid)
            return Prepared::failure({grid.error()});
        if(auto initialized = initialize(); !initialized)
            return initialized;
        const auto* prior = m_resources ? m_resources->buffers->source.get() : nullptr;
        const bool reuse_history =
            prior && m_resources->history->scene_lifetime == scene_lifetime
            && same_grid(m_resources->history->region, volume)
            && (prior->geometry == scene->geometry
                || (prior->geometry->topology
                    && prior->geometry->topology == scene->geometry->topology))
            && prior->materials == scene->materials
            && prior->environment_map == scene->environment_map
            && prior->environment == scene->environment && prior->background == scene->background;
        if(reuse_history && prior == scene.get())
            return Prepared::success();
        auto next = std::make_shared<Resources>();
        next->program = m_program;
        auto buffers =
            RaySceneBuffers::prepare(m_device, scene, m_resources ? m_resources->buffers : nullptr);
        if(!buffers)
            return Prepared::failure(buffers.error());
        next->buffers = std::move(buffers).value();
        if(reuse_history) {
            next->history = m_resources->history;
        } else {
            auto history = create_history(scene_lifetime, volume, grid.value());
            if(!history)
                return Prepared::failure(history.error());
            next->history = std::move(history).value();
        }
        if(auto bound = next->bind(m_device); !bound)
            return bound;
        m_statistics.upload_bytes += next->buffers->upload_bytes;
        m_statistics.geometry_upload_bytes += next->buffers->geometry_upload_bytes;
        m_resources = std::move(next);
        return Prepared::success();
    }

    Result<std::shared_ptr<Irradiance>, GraphicsError> RealtimeIrradiancePass::update(
        FrameScheduler& frames, uint64_t scene_lifetime,
        const std::shared_ptr<const RayScene>& scene, const RenderIrradianceVolume& volume,
        bool measure) {
        using Updated = Result<std::shared_ptr<Irradiance>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device)
            return Updated::failure({"Invalid realtime GI frame"});
        if(m_recorded_serial == frames.get_current_frame_serial())
            reset();
        const bool geometry_moved =
            scene && m_resources && m_resources->buffers->source->geometry != scene->geometry;
        if(auto ready = prepare(scene_lifetime, scene, volume); !ready)
            return Updated::failure(ready.error());
        auto& history = *m_resources->history;
        const uint32_t output = 1 - history.output;
        const auto& result = history.volumes[output];
        const uint32_t count = result->dimensions.x * result->dimensions.y * result->dimensions.z;
        const uint32_t budget = std::min(count, volume.update_budget);
        if(geometry_moved)
            history.visibility_refresh_remaining = count;
        const bool refresh_visibility = history.visibility_refresh_remaining > 0;
        const auto stages =
            Flags<PipelineStage>(PipelineStage::FragmentShader) | PipelineStage::ComputeShader;
        const auto compute = Flags<PipelineStage>(PipelineStage::ComputeShader);
        const auto range = ImageSubresourceRange{.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        const auto initial = history.initialized
                                 ? *resolve_image_state(ResourceUsage::SampledRead, range, stages)
                                 : ImageState{.subresources = range};
        RenderGraph graph;
        const auto source = graph.import_image("GI previous probes", initial);
        const auto destination = graph.import_image("GI updated probes", initial);
        std::optional<RenderGraph::PassId> clear;
        if(!history.initialized)
            clear = graph.add_pass({"initialize GI history",
                {{source, ResourceUsage::TransferDestination, {}},
                    {destination, ResourceUsage::TransferDestination, {}}}});
        const auto copy = graph.add_pass(
            {"preserve GI probes", {{source, ResourceUsage::TransferSource, {}},
                                       {destination, ResourceUsage::TransferDestination, {}}}});
        const auto trace = graph.add_pass(
            {"update GI probes", {{source, ResourceUsage::StorageRead, compute},
                                     {destination, ResourceUsage::StorageReadWrite, compute}}});
        graph.export_resource({source, ResourceUsage::SampledRead, stages});
        graph.export_resource({destination, ResourceUsage::SampledRead, stages});
        auto plan = graph.compile();
        if(!plan)
            return Updated::failure({plan.error()});
        const std::array<RenderGraph::Binding, 2> bindings{
            history.volumes[history.output]->probes->get_image_view()->get_image(),
            result->probes->get_image_view()->get_image()};
        const auto record = [&](RenderGraph::PassId pass, CommandBuffer& command) {
            const vk::ImageSubresourceRange color(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            if(clear && pass == *clear) {
                for(const auto& image : history.volumes)
                    command.get().clearColorImage(
                        image->probes->get_image_view()->get_image()->get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0}), color);
            } else if(pass == copy) {
                const vk::ImageSubresourceLayers layer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
                const vk::ImageCopy region(
                    layer, {}, layer, {}, {Irradiance::TEXELS_PER_PROBE, count, 1});
                command.get().copyImage(
                    history.volumes[history.output]->probes->get_image_view()->get_image()->get(),
                    vk::ImageLayout::eTransferSrcOptimal,
                    result->probes->get_image_view()->get_image()->get(),
                    vk::ImageLayout::eTransferDstOptimal, region);
            } else if(pass == trace) {
                const Parameters parameters{{result->origin, volume.max_distance},
                    {result->spacing, refresh_visibility ? std::min(volume.history_weight, .5f)
                                                         : volume.history_weight},
                    {Math::Vec3(result->dimensions), 0},
                    {2.0f * std::max({result->spacing.x, result->spacing.y, result->spacing.z}),
                        refresh_visibility ? 0.f : volume.history_weight, 0, 0},
                    Math::Vec4(scene->background, 1),
                    {history.cursor, budget, volume.update_rays, history.iteration},
                    {uint32_t(scene->geometry->nodes.size()),
                        uint32_t(scene->lighting->light_count), 0,
                        uint32_t(scene->emitters->size())},
                    scene->environment};
                const auto set = m_resources->descriptors[output].get();
                command.get().bindPipeline(
                    vk::PipelineBindPoint::eCompute, m_program->pipeline.get());
                command.get().bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                    m_program->layout.get(), 0, 1, &set, 0, nullptr);
                command.get().pushConstants(m_program->layout.get(),
                    vk::ShaderStageFlagBits::eCompute, 0, sizeof(parameters), &parameters);
                command.get().dispatch(budget, 1, 1);
            }
            return Result<void, GraphicsError>::success();
        };
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
        frames.retain_current_frame_resource(m_resources);
        if(auto recorded = plan.value().record(frames, bindings, record); !recorded)
            return Updated::failure(recorded.error());
        if(timing) {
            timing->timer->write(command, 1, GpuTimer::Boundary::End);
            timing->serial = frames.get_current_frame_serial();
        }
        history.initialized = true;
        // 首圈按连续索引更新；之后所有探针都已有求值，无须另存逐探针 CPU 状态。
        history.evaluated_probes = std::min(count, history.evaluated_probes + budget);
        history.visibility_refresh_remaining -=
            std::min(history.visibility_refresh_remaining, budget);
        history.output = output;
        m_statistics.first_updated_probe = history.cursor;
        history.cursor = (history.cursor + budget) % count;
        ++history.iteration;
        m_recorded_serial = frames.get_current_frame_serial();
        m_statistics.probes = count;
        m_statistics.evaluated_probes = history.evaluated_probes;
        m_statistics.updated_probes = budget;
        m_statistics.rays = budget * (volume.update_rays + 64);
        m_statistics.updates += budget;
        m_statistics.image_bytes =
            uint64_t(count) * Irradiance::TEXELS_PER_PROBE * sizeof(Math::Vec4) * 2;
        return Updated::success(result);
    }

    void RealtimeIrradiancePass::reset() {
        m_resources.reset();
        m_statistics = {};
        m_recorded_serial = 0;
        m_timings.clear();
    }
}
