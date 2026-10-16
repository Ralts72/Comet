#include "render/passes/path_trace_pass.h"

#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include "render/render_diagnostics.h"
#include "path_trace_comp.h"
#include "common/version.h"

#include <cstring>

namespace Comet {
    namespace {
        VersionSequence next_accumulation;
        struct Parameters {
            Math::Mat4 inverse_view_projection;
            Math::Vec4 background;
            std::array<uint32_t, 4> dimensions;
            std::array<uint32_t, 4> counts;
            Math::Vec4 environment;
        };
        static_assert(sizeof(Parameters) == 128);
        static_assert(sizeof(PathTraceScene::Triangle) == 208);
        static_assert(sizeof(PathTraceScene::Node) == 48);
        static_assert(sizeof(PathTraceScene::Surface) == 80);
        static_assert(sizeof(PathTraceScene::Texture) == 32);
    }

    struct PathTracePass::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
    };
    struct PathTracePass::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<const PathTraceScene> source;
        Math::Vec2u size;
        std::shared_ptr<ImageView> image;
        std::array<std::shared_ptr<ImageView>, 2> features;
        std::array<std::shared_ptr<CPUBuffer>, 8> buffers;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        bool initialized = false;
        bool features_enabled = false;
    };

    PathTracePass::PathTracePass(Device& device) : m_device(device) {}
    PathTracePass::~PathTracePass() = default;

    Result<void, GraphicsError> PathTracePass::check_support(const Device& device) {
        const auto& capability = device.get_capability();
        const auto limits = capability.physical_device.getProperties().limits;
        if(limits.maxPerStageDescriptorStorageBuffers < 8
            || limits.maxDescriptorSetStorageBuffers < 8
            || limits.maxPushConstantsSize < sizeof(Parameters)
            || limits.maxPerStageDescriptorStorageImages < 3
            || limits.maxDescriptorSetStorageImages < 3)
            return Result<void, GraphicsError>::failure(
                {"设备不支持路径追踪的 8 个 storage buffer／3 个 storage image／128 字节 push constants"});
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
        for(uint32_t i = 9; i < 11; ++i)
            bindings.add_binding(
                i, DescriptorType::StorageImage, Flags<ShaderStage>(ShaderStage::Compute));
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

    Result<void, GraphicsError> PathTracePass::prepare(
        const std::shared_ptr<const PathTraceScene>& scene, Math::Vec2u size, bool features) {
        features |= m_resources && m_resources->features_enabled;
        if(m_resources && m_resources->source == scene && m_resources->size == size
            && m_resources->features_enabled == features)
            return Result<void, GraphicsError>::success();
        if(auto initialized = initialize(); !initialized)
            return initialized;
        auto next = std::make_shared<Resources>();
        next->program = m_program;
        next->source = scene;
        next->size = size;
        next->features_enabled = features;
        const auto limits = m_device.get_capability().physical_device.getProperties().limits;
        if(size.x > limits.maxImageDimension2D || size.y > limits.maxImageDimension2D)
            return Result<void, GraphicsError>::failure({"路径追踪输入超过设备资源限制"});
        if(m_resources && m_resources->size == size && m_resources->features_enabled == features) {
            next->image = m_resources->image;
            next->features = m_resources->features;
            next->initialized = m_resources->initialized;
        } else {
            const std::array<std::shared_ptr<ImageView>*, 3> targets{
                &next->image, &next->features[0], &next->features[1]};
            for(std::size_t i = 0; i < targets.size(); ++i) {
                const auto extent = i == 0 || features ? size : Math::Vec2u(1);
                auto image = Image::try_create(m_device,
                    {Format::R32G32B32A32_SFLOAT, {extent.x, extent.y, 1},
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
                *targets[i] = std::move(view).value();
            }
        }
        const PathTraceScene::Node empty_node{};
        const PathTraceScene::Triangle empty_triangle{};
        const uint32_t empty_emitter = 0;
        const PathTraceScene::Surface empty_surface{};
        const PathTraceScene::Texture empty_texture{};
        const Math::Vec4 empty_texel{};
        const auto* previous = m_resources ? m_resources->source.get() : nullptr;
        const bool same_geometry = previous && previous->geometry == scene->geometry;
        const bool same_materials = previous && previous->materials == scene->materials;
        const std::array<bool, 8> reused{same_geometry, same_geometry,
            previous && previous->lighting == scene->lighting, same_geometry && same_materials,
            same_materials, same_materials, same_materials,
            previous && previous->environment_map == scene->environment_map};
        const auto& geometry = *scene->geometry;
        const auto& materials = *scene->materials;
        const auto& environment = *scene->environment_map;
        std::vector<std::byte> environment_bytes;
        if(!reused[7]) {
            environment_bytes.resize(16 + environment.texels.size() * sizeof(Math::Vec4));
            const std::array<uint32_t, 4> extent{environment.size.x, environment.size.y, 0, 0};
            std::memcpy(environment_bytes.data(), extent.data(), sizeof(extent));
            if(!environment.texels.empty())
                std::memcpy(environment_bytes.data() + 16, environment.texels.data(),
                    environment.texels.size() * sizeof(Math::Vec4));
        }
        const std::array<const void*, 8> sources{
            geometry.nodes.empty() ? &empty_node : geometry.nodes.data(),
            geometry.triangles.empty() ? &empty_triangle : geometry.triangles.data(),
            scene->lighting.get(),
            scene->emitters->empty() ? &empty_emitter : scene->emitters->data(),
            materials.surfaces.empty() ? &empty_surface : materials.surfaces.data(),
            materials.textures.empty() ? &empty_texture : materials.textures.data(),
            materials.texels.empty() ? &empty_texel : materials.texels.data(),
            environment_bytes.data()};
        const std::array<size_t, 8> sizes{
            std::max(size_t(1), geometry.nodes.size()) * sizeof(empty_node),
            std::max(size_t(1), geometry.triangles.size()) * sizeof(empty_triangle),
            sizeof(LightingData), std::max(size_t(1), scene->emitters->size()) * sizeof(uint32_t),
            std::max(size_t(1), materials.surfaces.size()) * sizeof(empty_surface),
            std::max(size_t(1), materials.textures.size()) * sizeof(empty_texture),
            std::max(size_t(1), materials.texels.size()) * sizeof(empty_texel),
            16 + environment.texels.size() * sizeof(Math::Vec4)};
        for(size_t i = 0; i < sources.size(); ++i) {
            if(reused[i]) {
                next->buffers[i] = m_resources->buffers[i];
                continue;
            }
            if(sizes[i] > limits.maxStorageBufferRange)
                return Result<void, GraphicsError>::failure(
                    {"路径追踪输入超过设备 storage buffer 限制"});
            auto buffer =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                    sizes[i], false, sources[i], "path trace scene");
            if(!buffer)
                return Result<void, GraphicsError>::failure(
                    {"Cannot allocate path trace scene", buffer.result()});
            next->buffers[i] = std::move(buffer).value();
        }
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::StorageBuffer, 8);
        pool_sizes.add_pool_size(DescriptorType::StorageImage, 3);
        auto pool = DescriptorPool::create(m_device, 1, pool_sizes);
        if(!pool)
            return Result<void, GraphicsError>::failure(pool.error());
        next->pool = std::move(pool).value();
        auto sets = next->pool->allocate_descriptor_set(*m_program->descriptors, 1);
        if(!sets)
            return Result<void, GraphicsError>::failure(sets.error());
        next->descriptors = std::move(sets).value();
        std::array<vk::DescriptorBufferInfo, 8> infos;
        std::array<vk::WriteDescriptorSet, 11> writes;
        for(uint32_t i = 0; i < infos.size(); ++i) {
            infos[i] = vk::DescriptorBufferInfo(next->buffers[i]->get(), 0, sizes[i]);
            writes[i] = {next->descriptors[0].get(), i >= 3 ? i + 1 : i, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]};
        }
        const std::array<std::shared_ptr<ImageView>, 3> images{
            next->image, next->features[0], next->features[1]};
        std::array<vk::DescriptorImageInfo, 3> image_infos;
        for(std::size_t i = 0; i < images.size(); ++i) {
            image_infos[i] = {{}, images[i]->get(), vk::ImageLayout::eGeneral};
            writes[8 + i] = {next->descriptors[0].get(), i == 0 ? 3u : uint32_t(8 + i), 0, 1,
                vk::DescriptorType::eStorageImage, &image_infos[i]};
        }
        m_device.get().updateDescriptorSets(writes, {});
        for(size_t i = 0; i < sizes.size(); ++i)
            if(!reused[i]) {
                m_upload_bytes += sizes[i];
                if(i < 2)
                    m_geometry_upload_bytes += sizes[i];
            }
        m_resources = std::move(next);
        m_samples = 0;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> PathTracePass::render(FrameScheduler& frames,
        const std::shared_ptr<const PathTraceScene>& scene, const ViewProjectMatrix& camera,
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
        if(auto ready = prepare(scene, settings.image_size(output_size), settings.denoised_view);
            !ready)
            return ready;
        if(view_projection != m_view_projection || settings.bounces != m_settings.bounces
            || settings.seed != m_settings.seed
            || settings.reset_generation != m_settings.reset_generation)
            m_samples = 0;
        m_view_projection = view_projection;
        m_settings = settings;
        if(m_samples == 0) {
            m_generation = next_accumulation.next();
            m_denoised.reset();
        }
        const bool dispatch = m_samples < settings.samples;
        if(dispatch)
            m_denoised.reset();
        frames.retain_current_frame_resource(m_resources);
        RenderGraph graph;
        const auto range = ImageSubresourceRange{.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        auto initial = m_resources->initialized
                           ? *resolve_image_state(ResourceUsage::SampledRead, range,
                                 Flags<PipelineStage>(PipelineStage::FragmentShader))
                           : ImageState{.subresources = range};
        const std::array views{
            m_resources->image, m_resources->features[0], m_resources->features[1]};
        std::vector<RenderGraph::Binding> bindings;
        std::vector<RenderGraph::Use> clear_uses, compute_uses;
        for(std::size_t i = 0; i < views.size(); ++i) {
            const auto image = graph.import_image("path trace input " + std::to_string(i), initial);
            bindings.push_back(views[i]->get_image());
            clear_uses.push_back({image, ResourceUsage::TransferDestination, {}});
            compute_uses.push_back({image, ResourceUsage::StorageReadWrite,
                Flags<PipelineStage>(PipelineStage::ComputeShader)});
            graph.export_resource({image, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        std::optional<RenderGraph::PassId> clear;
        if(!m_resources->initialized)
            clear = graph.add_pass({"initialize accumulation", std::move(clear_uses)});
        std::optional<RenderGraph::PassId> compute;
        if(dispatch)
            compute = graph.add_pass({"path trace", std::move(compute_uses)});
        RenderGraph::ResourceId display_image{0};
        if(m_settings.denoised_view && m_denoised) {
            display_image = graph.import_image(
                "denoised HDR", *resolve_image_state(ResourceUsage::SampledRead, range,
                                    Flags<PipelineStage>(PipelineStage::FragmentShader)));
            bindings.push_back(m_denoised->get_image());
            frames.retain_current_frame_resource(m_denoised);
        }
        graph.add_pass({"display", {{display_image, ResourceUsage::SampledRead,
                                       Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
        auto compiled = graph.compile();
        if(!compiled)
            return Result<void, GraphicsError>::failure({compiled.error()});
        const auto record = [&](RenderGraph::PassId pass, CommandBuffer& command) {
            if(clear && pass == *clear) {
                const vk::ImageSubresourceRange color(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                for(std::size_t i = 0; i < views.size(); ++i)
                    command.get().clearColorImage(views[i]->get_image()->get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0}), color);
                return Result<void, GraphicsError>::success();
            }
            if(!compute || pass != *compute)
                return display(pass, command);
            if(dispatch) {
                const auto size = m_resources->size;
                const Parameters parameters{inverse,
                    Math::Vec4(scene->background, m_resources->features_enabled ? 1 : 0),
                    {size.x, size.y, m_samples, settings.bounces},
                    {uint32_t(scene->geometry->nodes.size()),
                        uint32_t(scene->lighting->light_count), settings.seed,
                        uint32_t(scene->emitters->size())},
                    scene->environment};
                const auto vk_command = command.get();
                const auto set = m_resources->descriptors[0].get();
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
            m_recorded_serial = frames.get_current_frame_serial();
            m_samples += dispatch ? 1 : 0;
            m_dispatches += dispatch ? 1 : 0;
        }
        return recorded;
    }

    std::shared_ptr<ImageView> PathTracePass::output() const {
        if(m_settings.denoised_view && m_denoised)
            return m_denoised;
        return m_resources ? m_resources->image : nullptr;
    }
    PathTracePass::Statistics PathTracePass::statistics() const {
        if(!m_resources)
            return {};
        return {m_samples, uint32_t(m_resources->source->geometry->triangles.size()),
            uint32_t(m_resources->source->geometry->nodes.size()), m_resources->size,
            m_upload_bytes, m_geometry_upload_bytes, m_dispatches, m_generation, bool(m_denoised),
            m_resources->features_enabled};
    }
    std::array<std::shared_ptr<ImageView>, 3> PathTracePass::inputs() const {
        if(!m_resources)
            return {};
        return {m_resources->image,
            m_resources->features_enabled ? m_resources->features[0] : nullptr,
            m_resources->features_enabled ? m_resources->features[1] : nullptr};
    }
    bool PathTracePass::publish_denoised(
        std::shared_ptr<ImageView> image, uint64_t generation, uint32_t samples) {
        if(!m_resources || generation != m_generation || samples != m_samples || samples == 0)
            return false;
        m_denoised = std::move(image);
        return true;
    }
    void PathTracePass::reset() {
        m_denoised.reset();
        m_resources.reset();
        m_samples = 0;
        m_recorded_serial = 0;
        m_upload_bytes = 0;
        m_geometry_upload_bytes = 0;
        m_dispatches = 0;
    }
}
