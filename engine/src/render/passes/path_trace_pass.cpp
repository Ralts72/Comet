#include "render/passes/path_trace_pass.h"

#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/pipeline/shader.h"
#include "render/resource/ray_scene_buffers.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "render/passes/radiance_filter.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include "render/render_diagnostics.h"
#include "path_trace_comp.h"

namespace Comet {
    namespace {
        struct Parameters {
            Math::Mat4 inverse_view_projection;
            Math::Vec4 background;
            std::array<uint32_t, 4> dimensions;
            std::array<uint32_t, 4> counts;
            Math::Vec4 environment;
        };
        static_assert(sizeof(Parameters) == 128);
        struct CameraParameters {
            Math::Mat4 current, previous;
            Math::Vec4 medium, medium_color;
            std::array<uint32_t, 4> flags{};
        };
        static_assert(sizeof(CameraParameters) == 176);
        static_assert(sizeof(RayScene::Triangle) == 208);
        static_assert(sizeof(RayScene::Node) == 48);
        static_assert(sizeof(RayScene::Surface) == 112);
        static_assert(sizeof(RayScene::Texture) == 32);
    }

    struct PathTracePass::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
    };
    struct PathTracePass::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<const RayScene> source;
        Math::Vec2u size;
        std::shared_ptr<ImageView> image, guide, motion;
        bool guides = false;
        bool guides_initialized = false;
        std::vector<std::shared_ptr<CPUBuffer>> cameras;
        std::vector<std::shared_ptr<RaySceneBuffers>> previous;
        std::shared_ptr<RaySceneBuffers> buffers;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        bool initialized = false;
    };

    PathTracePass::PathTracePass(Device& device) : m_device(device) {}
    PathTracePass::~PathTracePass() = default;

    Result<void, GraphicsError> PathTracePass::check_support(const Device& device) {
        const auto& capability = device.get_capability();
        const auto limits = capability.physical_device.getProperties().limits;
        if(limits.maxPerStageDescriptorStorageBuffers < 9
            || limits.maxDescriptorSetStorageBuffers < 9
            || limits.maxPushConstantsSize < sizeof(Parameters))
            return Result<void, GraphicsError>::failure(
                {"设备不支持路径追踪的 9 个 storage buffer／128 字节 push constants"});
        const auto features =
            capability.physical_device.getFormatProperties(vk::Format::eR32G32B32A32Sfloat)
                .optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eStorageImage)
            || !(features & vk::FormatFeatureFlagBits::eSampledImage)
            || !(capability.physical_device.getQueueFamilyProperties()
                     .at(*capability.graphics_queue_family.queue_family_index)
                     .queueFlags
                 & vk::QueueFlagBits::eCompute))
            return Result<void, GraphicsError>::failure(
                {"设备不支持路径追踪所需的 compute／RGBA32F storage image"});
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> PathTracePass::initialize() {
        if(m_program)
            return Result<void, GraphicsError>::success();
        if(auto support = check_support(m_device); !support)
            return support;
        auto program = std::make_shared<Program>();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t i = 0; i < 3; ++i)
            bindings.add_binding(
                i, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            3, DescriptorType::StorageImage, Flags<ShaderStage>(ShaderStage::Compute));
        for(uint32_t i = 4; i < 9; ++i)
            bindings.add_binding(
                i, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            9, DescriptorType::StorageImage, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            10, DescriptorType::StorageImage, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            11, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            12, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(m_device, bindings);
        if(!descriptors)
            return Result<void, GraphicsError>::failure(descriptors.error());
        program->descriptors = std::move(descriptors).value();
        const auto set = program->descriptors->get();
        const vk::PushConstantRange constants(
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(Parameters));
        const vk::PipelineLayoutCreateInfo layout_info({}, 1, &set, 1, &constants);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            m_device.get(), "Create path trace layout", [&](auto* output) noexcept {
                return m_device.get().createPipelineLayout(&layout_info, nullptr, output);
            });
        if(!layout)
            return Result<void, GraphicsError>::failure(layout.error());
        program->layout = std::move(layout).value();
        auto shader = Shader::create(m_device, "software BVH path tracing", PATH_TRACE_COMP);
        if(!shader)
            return Result<void, GraphicsError>::failure(shader.error());
        const vk::PipelineShaderStageCreateInfo stage(
            {}, vk::ShaderStageFlagBits::eCompute, shader.value()->get(), "main");
        const vk::ComputePipelineCreateInfo info({}, stage, program->layout.get());
        auto pipeline = Graphics::create_handle<vk::Pipeline>(
            m_device.get(), "Create path trace pipeline", [&](auto* output) noexcept {
                return m_device.get().createComputePipelines({}, 1, &info, nullptr, output);
            });
        if(!pipeline)
            return Result<void, GraphicsError>::failure(pipeline.error());
        program->pipeline = std::move(pipeline).value();
        m_program = std::move(program);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> PathTracePass::prepare(const std::shared_ptr<const RayScene>& scene,
        Math::Vec2u size, uint32_t frame_slots, bool guides) {
        if(m_resources && m_resources->source == scene && m_resources->size == size
            && m_resources->guides == guides && m_resources->descriptors.size() == frame_slots)
            return Result<void, GraphicsError>::success();
        if(auto initialized = initialize(); !initialized)
            return initialized;
        auto next = std::make_shared<Resources>();
        next->program = m_program;
        next->source = scene;
        next->size = size;
        next->guides = guides;
        const auto limits = m_device.get_capability().physical_device.getProperties().limits;
        if(size.x > limits.maxImageDimension2D || size.y > limits.maxImageDimension2D)
            return Result<void, GraphicsError>::failure({"路径追踪输入超过设备资源限制"});
        if(m_resources && m_resources->size == size) {
            next->image = m_resources->image;
            next->initialized = m_resources->initialized;
        } else {
            auto image = Image::try_create(m_device,
                {Format::R32G32B32A32_SFLOAT, {size.x, size.y, 1},
                    Flags<ImageUsage>(ImageUsage::StorageBinding) | ImageUsage::Sampled
                        | ImageUsage::CopySrc | ImageUsage::CopyDst},
                false, SampleCount::Count1, "path trace accumulation");
            if(!image)
                return Result<void, GraphicsError>::failure(
                    {"Cannot allocate path trace image", image.result()});
            auto view = ImageView::try_create(
                m_device, std::move(image).value(), Flags<ImageAspect>(ImageAspect::Color));
            if(!view)
                return Result<void, GraphicsError>::failure(
                    {"Cannot create path trace image view", view.result()});
            next->image = std::move(view).value();
        }
        next->guide = next->motion = next->image;
        if(guides) {
            if(m_resources && m_resources->size == size && m_resources->guides) {
                next->guide = m_resources->guide;
                next->motion = m_resources->motion;
                next->guides_initialized = m_resources->guides_initialized;
            } else {
                for(auto* target : {&next->guide, &next->motion}) {
                    auto image = Image::try_create(m_device,
                        {Format::R32G32B32A32_SFLOAT, {size.x, size.y, 1},
                            Flags<ImageUsage>(ImageUsage::StorageBinding) | ImageUsage::Sampled},
                        false, SampleCount::Count1, "path trace guide");
                    if(!image)
                        return Result<void, GraphicsError>::failure(
                            {"Cannot allocate ray guide", image.result()});
                    auto view = ImageView::try_create(
                        m_device, std::move(image).value(), Flags<ImageAspect>(ImageAspect::Color));
                    if(!view)
                        return Result<void, GraphicsError>::failure(
                            {"Cannot create ray guide", view.result()});
                    *target = std::move(view).value();
                }
            }
        }
        auto buffers =
            RaySceneBuffers::prepare(m_device, scene, m_resources ? m_resources->buffers : nullptr);
        if(!buffers)
            return Result<void, GraphicsError>::failure(buffers.error());
        next->buffers = std::move(buffers).value();
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::StorageBuffer, 9 * frame_slots);
        pool_sizes.add_pool_size(DescriptorType::StorageImage, 3 * frame_slots);
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slots);
        auto pool = DescriptorPool::create(m_device, frame_slots, pool_sizes);
        if(!pool)
            return Result<void, GraphicsError>::failure(pool.error());
        next->pool = std::move(pool).value();
        auto sets = next->pool->allocate_descriptor_set(*m_program->descriptors, frame_slots);
        if(!sets)
            return Result<void, GraphicsError>::failure(sets.error());
        next->descriptors = std::move(sets).value();
        for(auto& descriptor : next->descriptors) {
            next->buffers->write(m_device, descriptor);
            auto buffer =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(CameraParameters), true, nullptr, "path trace camera");
            if(!buffer)
                return Result<void, GraphicsError>::failure(buffer.error());
            next->cameras.push_back(std::move(buffer).value());
            const std::array infos{
                vk::DescriptorImageInfo({}, next->image->get(), vk::ImageLayout::eGeneral),
                vk::DescriptorImageInfo({}, next->guide->get(), vk::ImageLayout::eGeneral),
                vk::DescriptorImageInfo({}, next->motion->get(), vk::ImageLayout::eGeneral)};
            std::array<vk::WriteDescriptorSet, 3> writes;
            constexpr std::array<uint32_t, 3> indices{3, 9, 10};
            for(size_t i = 0; i < writes.size(); ++i)
                writes[i] = {descriptor.get(), indices[i], 0, 1, vk::DescriptorType::eStorageImage,
                    &infos[i]};
            m_device.get().updateDescriptorSets(writes, {});
        }
        next->previous.resize(frame_slots);
        m_upload_bytes += next->buffers->upload_bytes;
        m_geometry_upload_bytes += next->buffers->geometry_upload_bytes;
        m_resources = std::move(next);
        m_samples = 0;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> PathTracePass::render(FrameScheduler& frames,
        const std::shared_ptr<const RayScene>& scene, const ViewProjectMatrix& camera,
        const PathTraceSettings& settings, Math::Vec2u output_size,
        const RenderGraph::RecordPass& display, RenderDiagnostics* diagnostics) {
        if(auto valid = settings.validate(); !valid)
            return Result<void, GraphicsError>::failure({valid.error()});
        if(!scene || !frames.is_recording_frame())
            return Result<void, GraphicsError>::failure({"Invalid path trace frame"});
        // serial 只在成功提交并结束帧后推进；未提交的录制不能计入积分。
        if(m_recorded_serial == frames.get_current_frame_serial())
            reset();
        const auto view_projection = camera.projection * camera.view;
        const auto inverse = Math::inverse(view_projection);
        if(!Math::is_finite(view_projection) || !Math::is_finite(inverse))
            return Result<void, GraphicsError>::failure({"Invalid path trace camera"});
        if(auto ready = prepare(scene, settings.image_size(output_size),
               frames.get_frame_slot_count(), settings.denoise);
            !ready)
            return ready;
        if(view_projection != m_view_projection || settings.bounces != m_settings.bounces
            || settings.seed != m_settings.seed
            || settings.reset_generation != m_settings.reset_generation
            || settings.dynamic != m_settings.dynamic
            || settings.medium_density != m_settings.medium_density
            || settings.medium_color != m_settings.medium_color
            || settings.medium_anisotropy != m_settings.medium_anisotropy)
            m_samples = 0;
        const auto slot = frames.get_current_frame_slot_index();
        const bool history_valid =
            m_previous_buffers && m_recorded_serial + 1 == frames.get_current_frame_serial()
            && m_previous_buffers->source->geometry->triangles.size()
                   == scene->geometry->triangles.size()
            && (m_previous_buffers->source->geometry == scene->geometry
                || (scene->geometry->topology
                    && m_previous_buffers->source->geometry->topology == scene->geometry->topology))
            && m_previous_buffers->source->materials->images == scene->materials->images
            && !camera.is_cut_from(m_previous_camera)
            && settings.reset_generation == m_settings.reset_generation
            && settings.bounces == m_settings.bounces && settings.seed == m_settings.seed
            && settings.medium_density == m_settings.medium_density
            && settings.medium_color == m_settings.medium_color
            && settings.medium_anisotropy == m_settings.medium_anisotropy;
        auto previous = history_valid ? m_previous_buffers : m_resources->buffers;
        m_resources->previous[slot] = previous;
        if(settings.dynamic)
            m_samples = 0;
        const auto samples = settings.dynamic
                                 ? settings.samples_per_frame
                                 : std::min(settings.samples_per_frame,
                                       settings.samples - std::min(m_samples, settings.samples));
        const CameraParameters camera_parameters{view_projection,
            m_previous_camera.projection * m_previous_camera.view,
            {settings.medium_density, settings.medium_anisotropy, 100, 0},
            Math::Vec4(settings.medium_color, 0),
            {history_valid ? 1u : 0u, settings.denoise ? 1u : 0u, samples, 0}};
        m_resources->cameras[slot]->write(&camera_parameters);
        const vk::DescriptorBufferInfo old_triangles(
            previous->buffers[1]->get(), 0, previous->sizes[1]);
        const vk::DescriptorBufferInfo camera_buffer(
            m_resources->cameras[slot]->get(), 0, sizeof(CameraParameters));
        const std::array buffer_writes{
            vk::WriteDescriptorSet(m_resources->descriptors[slot].get(), 11, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &old_triangles),
            vk::WriteDescriptorSet(m_resources->descriptors[slot].get(), 12, 0, 1,
                vk::DescriptorType::eUniformBuffer, nullptr, &camera_buffer)};
        m_device.get().updateDescriptorSets(buffer_writes, {});
        if(settings.denoise && !m_filter) {
            auto filter = RadianceFilter::create(m_device);
            if(!filter)
                return Result<void, GraphicsError>::failure(filter.error());
            m_filter = std::move(filter).value();
        }
        if(!settings.denoise && m_filter)
            m_filter->reset();
        m_view_projection = view_projection;
        frames.retain_current_frame_resource(m_resources);
        RenderGraph graph;
        const auto range = ImageSubresourceRange{.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        auto initial = m_resources->initialized
                           ? *resolve_image_state(ResourceUsage::SampledRead, range,
                                 Flags<PipelineStage>(PipelineStage::FragmentShader))
                           : ImageState{.subresources = range};
        const auto image = graph.import_image("path trace HDR", initial);
        std::optional<RenderGraph::PassId> clear;
        if(!m_resources->initialized)
            clear = graph.add_pass(
                {"initialize accumulation", {{image, ResourceUsage::TransferDestination, {}}}});
        std::array<RenderGraph::ResourceId, 2> guides{};
        if(settings.denoise) {
            const auto state = m_resources->guides_initialized
                                   ? *resolve_image_state(ResourceUsage::SampledRead, range,
                                         Flags<PipelineStage>(PipelineStage::FragmentShader))
                                   : ImageState{.subresources = range};
            guides[0] = graph.import_image("ray normal depth", state);
            guides[1] = graph.import_image("ray reprojection", state);
        }
        const bool dispatch = m_samples < settings.samples;
        std::optional<RenderGraph::PassId> compute;
        if(dispatch) {
            RenderGraph::Pass trace{
                "path trace", {{image, ResourceUsage::StorageReadWrite,
                                  Flags<PipelineStage>(PipelineStage::ComputeShader)}}};
            if(settings.denoise)
                for(auto id : guides)
                    trace.uses.push_back({id, ResourceUsage::StorageWrite,
                        Flags<PipelineStage>(PipelineStage::ComputeShader)});
            compute = graph.add_pass(std::move(trace));
        }
        RenderGraph::Pass show{
            "filter and display", {{image, ResourceUsage::SampledRead,
                                      Flags<PipelineStage>(PipelineStage::FragmentShader)}}};
        if(settings.denoise)
            for(auto id : guides)
                show.uses.push_back({id, ResourceUsage::SampledRead,
                    Flags<PipelineStage>(PipelineStage::FragmentShader)});
        const auto display_id = graph.add_pass(std::move(show));
        auto compiled = graph.compile();
        if(!compiled)
            return Result<void, GraphicsError>::failure({compiled.error()});
        std::vector<RenderGraph::Binding> bindings{m_resources->image->get_image()};
        if(settings.denoise) {
            bindings.emplace_back(m_resources->guide->get_image());
            bindings.emplace_back(m_resources->motion->get_image());
        }
        const auto record = [&](RenderGraph::PassId pass, CommandBuffer& command) {
            if(clear && pass == *clear) {
                const vk::ImageSubresourceRange color(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                command.get().clearColorImage(m_resources->image->get_image()->get(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0}), color);
                return Result<void, GraphicsError>::success();
            }
            if(pass == display_id) {
                m_settings.denoise = settings.denoise;
                if(settings.denoise) {
                    if(auto filtered = m_filter->render(frames, m_resources->image,
                           m_resources->guide, m_resources->motion, camera, m_previous_camera,
                           settings.history_weight, history_valid);
                        !filtered)
                        return filtered;
                }
                return display(pass, command);
            }
            if(!compute || pass != *compute)
                return Result<void, GraphicsError>::failure({"Unknown path trace pass"});
            if(dispatch) {
                const auto size = m_resources->size;
                const Parameters parameters{inverse, Math::Vec4(scene->background, 1),
                    {size.x, size.y, m_samples, settings.bounces},
                    {uint32_t(scene->geometry->nodes.size()),
                        uint32_t(scene->lighting->light_count),
                        settings.seed
                            ^ (settings.dynamic
                                    ? uint32_t(frames.get_current_frame_serial()) * 747796405u
                                    : 0u),
                        uint32_t(scene->emitters->size())},
                    scene->environment};
                const auto vk_command = command.get();
                const auto set = m_resources->descriptors[slot].get();
                vk_command.bindPipeline(vk::PipelineBindPoint::eCompute, m_program->pipeline.get());
                vk_command.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                    m_program->layout.get(), 0, 1, &set, 0, nullptr);
                vk_command.pushConstants(m_program->layout.get(), vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(parameters), &parameters);
                vk_command.dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);
            }
            return Result<void, GraphicsError>::success();
        };
        const auto recorded = diagnostics ? diagnostics->record(compiled.value(), bindings, record)
                                          : compiled.value().record(frames, bindings, record);
        if(recorded) {
            m_resources->initialized = true;
            if(settings.denoise)
                m_resources->guides_initialized = true;
            m_previous_camera = camera;
            m_previous_buffers = m_resources->buffers;
            m_settings = settings;
            m_recorded_serial = frames.get_current_frame_serial();
            m_samples += dispatch ? samples : 0;
            m_dispatches += dispatch ? 1 : 0;
        }
        return recorded;
    }

    std::shared_ptr<ImageView> PathTracePass::output() const {
        if(m_settings.denoise && m_filter && m_filter->output())
            return m_filter->output();
        return m_resources ? m_resources->image : nullptr;
    }
    PathTracePass::Statistics PathTracePass::statistics() const {
        if(!m_resources)
            return {};
        return {m_samples, uint32_t(m_resources->source->geometry->triangles.size()),
            uint32_t(m_resources->source->geometry->nodes.size()), m_resources->size,
            m_upload_bytes, m_geometry_upload_bytes, m_dispatches};
    }
    void PathTracePass::reset() {
        m_resources.reset();
        m_previous_buffers.reset();
        if(m_filter)
            m_filter->reset();
        m_samples = 0;
        m_recorded_serial = 0;
        m_upload_bytes = 0;
        m_geometry_upload_bytes = 0;
        m_dispatches = 0;
    }
}
