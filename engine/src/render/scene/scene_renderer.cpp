#include "render/scene/scene_renderer.h"
#include "common/scope_exit.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "render/render_graph.h"
#include "render/render_diagnostics.h"
#include "render/passes/output_pass.h"
#include "render/passes/bloom_pass.h"
#include "render/passes/shadow_pass.h"
#include "render/passes/skybox_pass.h"
#include "render/resource/texture.h"
#include "graphics/frame_buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/swapchain.h"
#include "graphics/attachment.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "render/debug/debug_renderer.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "render/passes/particle_pass.h"
#include "render/passes/outline_pass.h"
#include "render/passes/temporal_pass.h"
#include "render/passes/decal_pass.h"
#include "render/passes/deferred_pass.h"
#include "render/passes/screen_space_reflection_pass.h"
#include "render/passes/volumetric_fog_pass.h"

#include <algorithm>
#include <utility>
#include <tuple>

namespace Comet {
    // 配套绘制资源按依赖逆序析构，scene_pass 最后释放。
    // 在途帧保留整组资源；缩放会替换内部目标，因此还需单独保留本帧使用的目标。
    struct SceneRenderer::RenderState {
        std::shared_ptr<RenderPass> scene_pass;
        std::unique_ptr<PipelineManager> pipelines;
        std::unique_ptr<OutputPass> output_pass;
        std::unique_ptr<BloomPass> bloom;
        std::unique_ptr<DepthPass> view_depth;
        std::unique_ptr<TemporalPass> temporal;
        std::unique_ptr<ScreenSpaceReflectionPass> reflections;
        std::shared_ptr<ScreenSpaceReflectionPass::Resources> reflection_resources;
        std::optional<RenderGraph::PassId> reflection_pass;
        TemporalPass::Camera temporal_camera;
        std::unique_ptr<SkyboxPass> skybox_pass;
        std::shared_ptr<RenderTarget> hdr_target;
        std::shared_ptr<RenderTarget> output_target;
        std::unique_ptr<DeferredPass> deferred;
        std::shared_ptr<DeferredPass::Resources> deferred_resources;
        std::vector<RenderGraph::ResourceId> gbuffer_resources;
        std::optional<RenderGraph::PassId> gbuffer_pass;
        std::unique_ptr<MaterialRenderer> materials;
        RenderGeometry view_geometry;
        std::unique_ptr<DebugRenderer> debug;
        std::unique_ptr<ParticlePass> particles;
        std::unique_ptr<OutlinePass> outline;
        std::shared_ptr<OutlinePass::Resources> outline_resources;
        std::array<RenderGraph::ResourceId, 3> outline_masks;
        std::optional<RenderGraph::PassId> outline_mask_pass, outline_composite_pass;
        std::unique_ptr<DecalPass> decals;
        std::unique_ptr<VolumetricFogPass> fog;
        std::shared_ptr<VolumetricFogPass::Resources> fog_resources;
        FogVolume fog_frame;
        std::optional<RenderGraph::PassId> fog_pass;
        std::unique_ptr<PathTracePass> path_trace;
        RenderGraph::Plan graph;
        RenderGraph::Resources transient_resources;
        std::vector<RenderGraph::Binding> bindings;
        RenderGraph::ResourceId shadow_resource;
        std::vector<RenderGraph::ResourceId> scene_attachments;
        std::optional<RenderGraph::PassId> shadow_pass_id;
        ViewRequirements requirements;
        RenderGraph::PassId scene_pass_id;
        std::optional<RenderGraph::PassId> gpu_particles_pass;
        RenderGraph::PassId output_pass_id;
        std::optional<BloomPass::Passes> bloom_passes;
        std::optional<RenderGraph::ResourceId> view_depth_resource;
        std::optional<RenderGraph::PassId> view_depth_pass;
        std::optional<RenderGraph::ResourceId> motion_resource;
        std::optional<RenderGraph::ResourceId> temporal_output;
        std::optional<RenderGraph::ResourceId> temporal_history;
        std::optional<RenderGraph::PassId> temporal_pass;
        bool offscreen = false;
        QualitySettings quality;
        IrradianceDebugMode irradiance_debug = IrradianceDebugMode::Off;
    };

    Result<std::unique_ptr<SceneRenderer>, GraphicsError> SceneRenderer::create(Device& device,
        MaterialPrograms& programs, RenderResources& resources, const VulkanSettings& vulkan,
        const RenderSettings& render, Math::Vec2u size) {
        using Creation = Result<std::unique_ptr<SceneRenderer>, GraphicsError>;
        auto scene =
            std::unique_ptr<SceneRenderer>(new SceneRenderer(device, programs, vulkan, render));
        if(auto configured = scene->configure_offscreen(resources, size); !configured)
            return Creation::failure(configured.error());
        return Creation::success(std::move(scene));
    }

    Result<std::unique_ptr<SceneRenderer>, GraphicsError> SceneRenderer::create(Device& device,
        MaterialPrograms& programs, RenderResources& resources, const VulkanSettings& vulkan,
        const RenderSettings& render, Swapchain& swapchain) {
        if(render.scene_output == RenderSettings::SceneOutput::Offscreen)
            return create(device, programs, resources, vulkan, render,
                Math::Vec2u{swapchain.get_width(), swapchain.get_height()});
        using Creation = Result<std::unique_ptr<SceneRenderer>, GraphicsError>;
        auto scene =
            std::unique_ptr<SceneRenderer>(new SceneRenderer(device, programs, vulkan, render));
        if(auto configured = scene->configure_presentation(resources, swapchain); !configured)
            return Creation::failure(configured.error());
        return Creation::success(std::move(scene));
    }

    SceneRenderer::SceneRenderer(Device& device, MaterialPrograms& programs,
        const VulkanSettings& vulkan, const RenderSettings& render)
        : m_device(device), m_programs(programs), m_offscreen_format(vulkan.surface_format),
          m_hdr_headroom(render.hdr_headroom), m_hdr_white_level(render.hdr_white_level),
          m_depth_format(vulkan.depth_format),
          m_quality{static_cast<uint32_t>(vulkan.msaa_samples),
              std::min(render.max_anisotropy, device.get_capability().max_sampler_anisotropy),
              render.render_scale, render.temporal_aa, render.render_path},
          m_frame_slot_count(render.max_frames_in_flight) {}

    Result<std::shared_ptr<SceneRenderer::RenderState>, GraphicsError> SceneRenderer::create_state(
        RenderResources& resources, Swapchain* swapchain, Math::Vec2u size,
        const QualitySettings& quality) {
        using Creation = Result<std::shared_ptr<RenderState>, GraphicsError>;
        if(m_frame_slot_count == 0)
            return Creation::failure({"Scene renderer requires at least one frame slot"});
        if(auto valid = m_post_process.validate(); !valid)
            return Creation::failure({valid.error()});
        if(!swapchain && (size.x == 0 || size.y == 0))
            return Creation::failure({"Offscreen render target size must be greater than zero"});
        if(auto supported = validate_quality_settings(quality); !supported)
            return Creation::failure(supported.error());
        const auto samples = static_cast<SampleCount>(quality.msaa_samples);
        auto next = std::make_shared<RenderState>();
        next->quality = quality;
        if(m_state)
            next->requirements = m_state->requirements;
        next->offscreen = !swapchain;
        auto color = Attachment::get_color_attachment(RenderSettings::SCENE_COLOR_FORMAT, samples);
        auto depth = Attachment::get_depth_attachment(
            quality.render_path == RenderPath::Deferred ? Format::D32_SFLOAT : m_depth_format,
            samples);
        if(quality.render_path == RenderPath::Deferred) {
            depth.description.store_op = AttachmentStoreOp::Store;
            depth.usage |= ImageUsage::Sampled;
        }
        color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        if(samples == SampleCount::Count1) {
            color.description.store_op = AttachmentStoreOp::Store;
            color.usage |= ImageUsage::Sampled;
            color.usage |= ImageUsage::CopySrc;
        }
        RenderSubPass subpass{.color_attachments = {SubpassColorAttachment(0)},
            .depth_stencil_attachments = {SubpassDepthStencilAttachment(1)},
            .sample_count = samples};
        subpass.resolve_initial_layout = ImageLayout::ColorAttachmentOptimal;
        subpass.resolve_final_layout = ImageLayout::ColorAttachmentOptimal;
        subpass.resolve_usage = Flags<ImageUsage>(ImageUsage::ColorAttachment) | ImageUsage::Sampled
                                | ImageUsage::CopySrc;
        auto pass = RenderPass::create(
            m_device, {color, depth}, {subpass}, RenderSettings::SCENE_COLOR_FORMAT);
        if(!pass)
            return Creation::failure(pass.error());
        next->scene_pass = std::move(pass).value();
        vk::SurfaceFormatKHR surface_output;
        if(swapchain)
            surface_output = swapchain->get_active_generation()->get_config().surface_format;
        else
            surface_output = vk::SurfaceFormatKHR{
                Graphics::format_to_vk(m_offscreen_format), vk::ColorSpaceKHR::eSrgbNonlinear};
        auto output_pass = OutputPass::create(m_device,
            Graphics::vk_to_format(surface_output.format), next->offscreen, m_frame_slot_count,
            Graphics::vk_to_image_color_space(surface_output.colorSpace), m_hdr_headroom,
            m_hdr_white_level);
        if(!output_pass)
            return Creation::failure(output_pass.error());
        next->output_pass = std::move(output_pass).value();
        if(quality.temporal_aa) {
            auto temporal = TemporalPass::create(m_device, m_frame_slot_count);
            if(!temporal)
                return Creation::failure(temporal.error());
            next->temporal = std::move(temporal).value();
        }
        if(!m_shadow) {
            auto shadow = ShadowPass::create(m_device, m_frame_slot_count);
            if(!shadow)
                return Creation::failure(shadow.error());
            m_shadow = std::move(shadow).value();
        }
        if(m_post_process.uses_bloom()) {
            auto bloom = BloomPass::create(m_device, m_frame_slot_count);
            if(!bloom)
                return Creation::failure(bloom.error());
            next->bloom = std::move(bloom).value();
        }
        next->pipelines = std::make_unique<PipelineManager>(m_device, *next->scene_pass);
        auto skybox = SkyboxPass::create(m_device, *next->pipelines, samples, m_frame_slot_count);
        if(!skybox)
            return Creation::failure(skybox.error());
        next->skybox_pass = std::move(skybox).value();
        if(quality.render_path == RenderPath::Deferred) {
            auto deferred = DeferredPass::create(m_device, m_frame_slot_count);
            if(!deferred)
                return Creation::failure(deferred.error());
            next->deferred = std::move(deferred).value();
        }
        auto materials = MaterialRenderer::create(m_device, *next->pipelines, resources,
            m_frame_slot_count, samples, m_programs.builtin_overrides(), &m_programs,
            quality.max_anisotropy, next->deferred ? &next->deferred->pipelines() : nullptr);
        if(!materials)
            return Creation::failure(materials.error());
        next->materials = std::move(materials).value();
        if(next->deferred) {
            auto lighting = next->deferred->create_lighting(
                *next->pipelines, next->materials->view_bindings().layout());
            if(!lighting)
                return Creation::failure(lighting.error());
        }
        if(next->deferred && m_post_process.uses_reflections()) {
            auto reflections = ScreenSpaceReflectionPass::create(
                m_device, m_frame_slot_count, next->materials->view_bindings().layout());
            if(!reflections)
                return Creation::failure(reflections.error());
            next->reflections = std::move(reflections).value();
        }
        auto debug = DebugRenderer::create(m_device, *next->pipelines, m_frame_slot_count, samples,
            next->materials->view_bindings().layout());
        if(!debug)
            return Creation::failure(debug.error());
        next->debug = std::move(debug).value();
        auto particles = ParticlePass::create(
            m_device, *next->pipelines, resources, m_frame_slot_count, samples);
        if(!particles)
            return Creation::failure(particles.error());
        next->particles = std::move(particles).value();
        if(next->requirements.fog) {
            auto fog = VolumetricFogPass::create(
                m_device, *next->pipelines, next->materials->view_bindings().layout(), samples);
            if(!fog)
                return Creation::failure(fog.error());
            next->fog = std::move(fog).value();
        }
        if(auto targets = replace_targets(*next, swapchain, size); !targets)
            return Creation::failure(targets.error());
        return Creation::success(std::move(next));
    }

    Result<void, GraphicsError> SceneRenderer::rebuild_graph(RenderState& state,
        const PostProcessSettings& settings, const ViewRequirements& requirements,
        EffectPasses effects, Math::Vec2u source_size) {
        RenderGraph graph;
        // 丢弃旧内容仍需等待上一轮附件写入与片段采样。
        auto shadow_access = *resolve_resource_state(ResourceUsage::DepthStencilAttachmentWrite);
        shadow_access.stages |= PipelineStage::FragmentShader;
        shadow_access.stages |= PipelineStage::ComputeShader;
        ImageState shadow_initial{.resource = shadow_access,
            .subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Depth)}};
        const auto fallback_initial = *resolve_image_state(ResourceUsage::SampledRead,
            {.aspects = Flags<ImageAspect>(ImageAspect::Depth)},
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        const auto shadow = graph.import_image(
            "shadow depth", requirements.shadows ? shadow_initial : fallback_initial);
        std::optional<RenderGraph::PassId> shadow_pass_id;
        if(requirements.shadows) {
            RenderGraph::Pass shadow_pass{"shadow", {}};
            shadow_pass.uses.push_back({shadow, ResourceUsage::DepthStencilAttachmentWrite, {}});
            shadow_pass_id = graph.add_pass(std::move(shadow_pass));
        }
        std::optional<RenderGraph::ResourceId> view_depth_resource;
        std::optional<RenderGraph::PassId> view_depth_pass;
        std::optional<RenderGraph::ResourceId> motion_resource;
        if(requirements.needs_depth(settings, bool(state.temporal))) {
            view_depth_resource = graph.import_image("view depth", shadow_initial);
            RenderGraph::Pass depth_pass{"view depth",
                {{*view_depth_resource, ResourceUsage::DepthStencilAttachmentWrite, {}}}};
            if(state.temporal) {
                auto access = *resolve_resource_state(ResourceUsage::ColorAttachmentWrite);
                access.stages |= PipelineStage::FragmentShader;
                motion_resource = graph.import_image("motion vectors",
                    {.resource = access,
                        .subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Color)}});
                depth_pass.uses.push_back(
                    {*motion_resource, ResourceUsage::ColorAttachmentWrite, {}});
            }
            view_depth_pass = graph.add_pass(std::move(depth_pass));
        }
        std::optional<RenderGraph::ResourceId> fog_resource;
        std::optional<RenderGraph::PassId> fog_pass;
        if(requirements.fog) {
            const auto size = VolumetricFogPass::atlas_size(source_size);
            fog_resource =
                graph.create_image("volumetric fog", {Format::R16G16B16A16_SFLOAT, size.x, size.y});
            fog_pass = graph.add_pass(
                {"volumetric fog", {{shadow, ResourceUsage::SampledRead,
                                        Flags<PipelineStage>(PipelineStage::ComputeShader)},
                                       {*fog_resource, ResourceUsage::StorageWrite,
                                           Flags<PipelineStage>(PipelineStage::ComputeShader)}}});
        }
        std::vector<RenderGraph::ResourceId> gbuffer_resources;
        std::optional<RenderGraph::PassId> gbuffer_pass;
        if(state.deferred) {
            RenderGraph::Pass geometry{"G-buffer", {}};
            for(uint32_t index = 0; index <= DeferredPass::COLOR_COUNT; ++index) {
                const bool depth = index == DeferredPass::COLOR_COUNT;
                const auto usage = depth ? ResourceUsage::DepthStencilAttachmentWrite
                                         : ResourceUsage::ColorAttachmentWrite;
                auto access = *resolve_resource_state(usage);
                access.stages |= PipelineStage::FragmentShader;
                const auto resource = graph.import_image("G-buffer " + std::to_string(index),
                    {.resource = access,
                        .subresources = {.aspects = Flags<ImageAspect>(
                                             depth ? ImageAspect::Depth : ImageAspect::Color)}});
                gbuffer_resources.push_back(resource);
                geometry.uses.push_back({resource, usage, {}});
            }
            gbuffer_pass = graph.add_pass(std::move(geometry));
        }
        RenderGraph::Pass scene{state.deferred ? "deferred lighting and Forward" : "scene", {}};
        for(const auto resource : gbuffer_resources)
            scene.uses.push_back({resource, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        if(fog_resource)
            scene.uses.push_back({*fog_resource, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        if(view_depth_resource)
            scene.uses.push_back({*view_depth_resource, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        std::vector<RenderGraph::ResourceId> scene_attachments;
        RenderGraph::ResourceId output;
        for(const auto& attachment : state.scene_pass->get_attachments()) {
            const bool is_depth = Graphics::is_depth_stencil_format(attachment.description.format);
            auto aspects = Flags<ImageAspect>(is_depth ? ImageAspect::Depth : ImageAspect::Color);
            if(is_depth && !Graphics::is_depth_only_format(attachment.description.format))
                aspects |= ImageAspect::Stencil;
            auto usage = ResourceUsage::ColorAttachmentWrite;
            if(is_depth)
                usage = ResourceUsage::DepthStencilAttachmentWrite;
            auto previous_access = *resolve_resource_state(usage);
            if(!is_depth || state.deferred)
                previous_access.stages |= PipelineStage::FragmentShader;
            const auto id = graph.import_image("attachment " + std::to_string(scene.uses.size()),
                {.resource = previous_access, .subresources = {.aspects = aspects}});
            scene_attachments.push_back(id);
            scene.uses.push_back({id, usage, {}});
            if(!is_depth)
                output = id;
        }
        scene.uses.push_back({shadow, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        // 粒子 buffer 的阶段内同步由模拟器负责；有序图在 HDR 绘制前调度并计时。
        std::optional<RenderGraph::PassId> gpu_particles_pass;
        if(requirements.gpu_particles)
            gpu_particles_pass = graph.add_pass({"GPU particles", {}});
        const auto scene_pass_id = graph.add_pass(std::move(scene));
        std::optional<RenderGraph::ResourceId> reflection_resource;
        std::optional<RenderGraph::PassId> reflection_pass;
        if(state.deferred && settings.uses_reflections()) {
            reflection_resource = graph.create_image(
                "reflected HDR", {Format::R16G16B16A16_SFLOAT, source_size.x, source_size.y});
            const Flags<PipelineStage> fragment(PipelineStage::FragmentShader);
            RenderGraph::Pass reflection{"screen space reflections",
                {{output, ResourceUsage::SampledRead, fragment},
                    {scene_attachments.at(1), ResourceUsage::SampledRead, fragment},
                    {*reflection_resource, ResourceUsage::ColorAttachmentWrite, {}}}};
            for(const auto resource : gbuffer_resources)
                reflection.uses.push_back({resource, ResourceUsage::SampledRead, fragment});
            reflection.uses.push_back({shadow, ResourceUsage::SampledRead, fragment});
            if(view_depth_resource)
                reflection.uses.push_back(
                    {*view_depth_resource, ResourceUsage::SampledRead, fragment});
            if(fog_resource)
                reflection.uses.push_back({*fog_resource, ResourceUsage::SampledRead, fragment});
            reflection_pass = graph.add_pass(std::move(reflection));
            output = *reflection_resource;
        }
        std::optional<RenderGraph::ResourceId> temporal_output, temporal_history;
        std::optional<RenderGraph::PassId> temporal_pass;
        if(state.temporal) {
            const auto sampled = *resolve_image_state(ResourceUsage::SampledRead,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                Flags<PipelineStage>(PipelineStage::FragmentShader));
            temporal_output = graph.import_image("temporal output", sampled);
            temporal_history = graph.import_image("temporal history", sampled);
            temporal_pass = graph.add_pass({"temporal resolve",
                {{output, ResourceUsage::SampledRead,
                     Flags<PipelineStage>(PipelineStage::FragmentShader)},
                    {*view_depth_resource, ResourceUsage::SampledRead,
                        Flags<PipelineStage>(PipelineStage::FragmentShader)},
                    {*motion_resource, ResourceUsage::SampledRead,
                        Flags<PipelineStage>(PipelineStage::FragmentShader)},
                    {*temporal_history, ResourceUsage::SampledRead,
                        Flags<PipelineStage>(PipelineStage::FragmentShader)},
                    {*temporal_output, ResourceUsage::ColorAttachmentWrite, {}}}});
            output = *temporal_output;
        }
        std::array<RenderGraph::ResourceId, 3> outline_masks;
        std::optional<RenderGraph::PassId> outline_mask_pass, outline_composite_pass;
        std::optional<RenderGraph::ResourceId> outline_output;
        if(requirements.outline) {
            RenderGraph::Pass mask{"outline mask", {}};
            for(uint32_t i = 0; i < 3; ++i) {
                const auto usage = i == 0 ? ResourceUsage::DepthStencilAttachmentWrite
                                          : ResourceUsage::ColorAttachmentWrite;
                auto initial = *resolve_resource_state(usage);
                initial.stages |= PipelineStage::FragmentShader;
                outline_masks[i] = graph.import_image("outline mask " + std::to_string(i),
                    {.resource = initial,
                        .subresources = {.aspects = Flags<ImageAspect>(
                                             i == 0 ? ImageAspect::Depth : ImageAspect::Color)}});
                mask.uses.push_back({outline_masks[i], usage, {}});
            }
            outline_mask_pass = graph.add_pass(std::move(mask));
            outline_output = graph.create_image(
                "outlined HDR", {Format::R16G16B16A16_SFLOAT, source_size.x, source_size.y});
            const Flags<PipelineStage> fragment(PipelineStage::FragmentShader);
            RenderGraph::Pass composite{"outline composite",
                {{output, ResourceUsage::SampledRead, fragment},
                    {*view_depth_resource, ResourceUsage::SampledRead, fragment},
                    {*outline_output, ResourceUsage::ColorAttachmentWrite, {}}}};
            for(auto image : outline_masks)
                composite.uses.push_back({image, ResourceUsage::SampledRead, fragment});
            outline_composite_pass = graph.add_pass(std::move(composite));
            output = *outline_output;
        }
        std::optional<BloomPass::Passes> bloom_passes;
        RenderGraph::Pass display{
            "display", {{output, ResourceUsage::SampledRead,
                           Flags<PipelineStage>(PipelineStage::FragmentShader)}}};
        if(settings.uses_bloom()) {
            bloom_passes = BloomPass::append_passes(graph, output, source_size);
            display.uses.push_back({bloom_passes->output, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        const auto output_pass_id = graph.add_pass(std::move(display));
        auto compiled = graph.compile();
        if(!compiled)
            return Result<void, GraphicsError>::failure({compiled.error()});
        auto resources = compiled.value().allocate(m_device, m_frame_slot_count);
        if(!resources)
            return Result<void, GraphicsError>::failure(resources.error());
        std::shared_ptr<VolumetricFogPass::Resources> fog_resources;
        if(fog_resource) {
            auto prepared =
                effects.fog->create_resources(resources.value(), *fog_resource, m_frame_slot_count);
            if(!prepared)
                return Result<void, GraphicsError>::failure(prepared.error());
            fog_resources = std::move(prepared).value();
        }
        std::shared_ptr<ScreenSpaceReflectionPass::Resources> reflection_resources;
        if(reflection_resource) {
            auto prepared = effects.reflections->create_resources(
                resources.value(), *reflection_resource, source_size);
            if(!prepared)
                return Result<void, GraphicsError>::failure(prepared.error());
            reflection_resources = std::move(prepared).value();
        }
        if(bloom_passes) {
            if(auto prepared =
                    effects.bloom->prepare(resources.value(), *bloom_passes, source_size);
                !prepared)
                return prepared;
        }
        std::shared_ptr<OutlinePass::Resources> outline_resources;
        if(outline_output) {
            auto prepared =
                effects.outline->create_resources(resources.value(), *outline_output, source_size);
            if(!prepared)
                return Result<void, GraphicsError>::failure(prepared.error());
            outline_resources = std::move(prepared).value();
        }
        state.outline_resources = std::move(outline_resources);
        state.outline_masks = outline_masks;
        state.outline_mask_pass = outline_mask_pass;
        state.outline_composite_pass = outline_composite_pass;
        state.reflection_resources = std::move(reflection_resources);
        state.reflection_pass = reflection_pass;
        state.gbuffer_resources = std::move(gbuffer_resources);
        state.gbuffer_pass = gbuffer_pass;
        state.fog_resources = std::move(fog_resources);
        state.fog_pass = fog_pass;
        state.graph = std::move(compiled).value();
        state.transient_resources = std::move(resources).value();
        state.shadow_resource = shadow;
        state.scene_attachments = std::move(scene_attachments);
        state.shadow_pass_id = shadow_pass_id;
        state.requirements = requirements;
        state.scene_pass_id = scene_pass_id;
        state.gpu_particles_pass = gpu_particles_pass;
        state.output_pass_id = output_pass_id;
        state.bloom_passes = bloom_passes;
        state.view_depth_resource = view_depth_resource;
        state.view_depth_pass = view_depth_pass;
        state.motion_resource = motion_resource;
        state.temporal_output = temporal_output;
        state.temporal_history = temporal_history;
        state.temporal_pass = temporal_pass;
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::prepare_post_process(
        const PostProcessSettings& settings, const std::chrono::steady_clock::time_point now) {
        if(auto valid = settings.validate(); !valid)
            return Result<void, GraphicsError>::failure({valid.error()});
        if(!m_state)
            return Result<void, GraphicsError>::failure({"Scene renderer is not configured"});
        const auto size = m_state->output_target->get_size();
        if(settings.uses_bloom() == m_post_process.uses_bloom()
            && settings.uses_ao() == m_post_process.uses_ao()
            && settings.uses_reflections() == m_post_process.uses_reflections()) {
            m_post_process_failure.reset();
            m_post_process = settings;
            return Result<void, GraphicsError>::success();
        }
        if(m_post_process_failure && m_post_process_failure->size != size)
            m_post_process_failure.reset();
        if(m_post_process_failure && !m_post_process_failure->retry.consume(now))
            return Result<void, GraphicsError>::success();
        const auto prepared = configure_effects(settings, m_state->requirements);
        if(prepared) {
            m_post_process = settings;
            m_post_process_failure.reset();
            return prepared;
        }
        if(!prepared.error().is_out_of_memory())
            return prepared;
        if(!m_post_process_failure)
            m_post_process_failure = TargetRetry{size};
        auto& retry = m_post_process_failure->retry;
        if(!retry.schedule(now))
            LOG_ERROR("Post-process preparation exhausted retries; keeping previous settings: {}",
                prepared.error().message);
        else if(retry.retry_count() == 1)
            LOG_WARN("Post-process preparation will retry; keeping previous settings: {}",
                prepared.error().message);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::prepare_material_programs(const RenderWorld& world) {
        if(!m_state)
            return Result<void, GraphicsError>::failure({"Scene renderer is not configured"});
        return m_state->materials->prepare_programs(world);
    }

    Result<void, GraphicsError> SceneRenderer::warmup_scene(const RenderWorld& world) {
        if(!m_state)
            return Result<void, GraphicsError>::failure({"Scene renderer is not configured"});
        return m_state->materials->warmup_scene(world);
    }

    Result<void, GraphicsError> SceneRenderer::configure_effects(
        const PostProcessSettings& settings, const ViewRequirements& requirements) {
        std::unique_ptr<VolumetricFogPass> fog_candidate;
        if(requirements.fog && !m_state->fog) {
            auto created = VolumetricFogPass::create(m_device, *m_state->pipelines,
                m_state->materials->view_bindings().layout(),
                static_cast<SampleCount>(m_state->quality.msaa_samples));
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            fog_candidate = std::move(created).value();
        }
        std::unique_ptr<DecalPass> decal_candidate;
        if(requirements.decals && !m_state->decals) {
            auto created = DecalPass::create(m_device, *m_state->pipelines,
                m_state->materials->view_bindings().fallback_texture(),
                m_state->materials->view_bindings().layout(), m_frame_slot_count,
                static_cast<SampleCount>(m_state->quality.msaa_samples));
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            decal_candidate = std::move(created).value();
        }
        std::unique_ptr<ScreenSpaceReflectionPass> reflection_candidate;
        if(m_state->deferred && settings.uses_reflections() && !m_state->reflections) {
            auto created = ScreenSpaceReflectionPass::create(
                m_device, m_frame_slot_count, m_state->materials->view_bindings().layout());
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            reflection_candidate = std::move(created).value();
        }
        std::unique_ptr<OutlinePass> outline_candidate;
        if(requirements.outline && !m_state->outline) {
            auto created =
                OutlinePass::create(m_device, m_frame_slot_count, m_state->hdr_target->get_size());
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            outline_candidate = std::move(created).value();
        }
        std::unique_ptr<BloomPass> candidate;
        if(settings.uses_bloom() && !m_state->bloom) {
            auto created = BloomPass::create(m_device, m_frame_slot_count);
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            candidate = std::move(created).value();
        }
        std::unique_ptr<DepthPass> depth_candidate;
        if(requirements.needs_depth(settings, bool(m_state->temporal))) {
            auto* depth = m_state->view_depth.get();
            if(!depth) {
                auto created = DepthPass::create(m_device, m_frame_slot_count,
                    m_state->hdr_target->get_size(), 1,
                    m_state->temporal ? DepthPass::Output::Motion : DepthPass::Output::Depth);
                if(!created)
                    return Result<void, GraphicsError>::failure(created.error());
                depth_candidate = std::move(created).value();
                depth = depth_candidate.get();
            }
            if(auto resized = depth->resize(m_state->hdr_target->get_size()); !resized)
                return resized;
        }
        const auto graph_needs = [&](const PostProcessSettings& post,
                                     const ViewRequirements& view) {
            return std::tuple(view.shadows, view.needs_depth(post, bool(m_state->temporal)),
                view.fog, view.outline, view.gpu_particles, post.uses_bloom(),
                bool(m_state->deferred) && post.uses_reflections());
        };
        if(graph_needs(settings, requirements)
            != graph_needs(m_post_process, m_state->requirements)) {
            if(auto graph = rebuild_graph(*m_state, settings, requirements,
                   {candidate ? candidate.get() : m_state->bloom.get(),
                       fog_candidate ? fog_candidate.get() : m_state->fog.get(),
                       reflection_candidate ? reflection_candidate.get()
                                            : m_state->reflections.get(),
                       outline_candidate ? outline_candidate.get() : m_state->outline.get()},
                   m_state->hdr_target->get_size());
                !graph)
                return graph;
        }
        m_state->requirements = requirements;
        if(outline_candidate)
            m_state->outline = std::move(outline_candidate);
        if(!requirements.outline)
            m_state->outline.reset();
        if(reflection_candidate)
            m_state->reflections = std::move(reflection_candidate);
        if(!settings.uses_reflections())
            m_state->reflections.reset();
        if(fog_candidate)
            m_state->fog = std::move(fog_candidate);
        if(!requirements.fog)
            m_state->fog.reset();
        if(depth_candidate)
            m_state->view_depth = std::move(depth_candidate);
        if(!requirements.needs_depth(settings, bool(m_state->temporal)))
            m_state->view_depth.reset();
        if(candidate)
            m_state->bloom = std::move(candidate);
        if(decal_candidate)
            m_state->decals = std::move(decal_candidate);
        if(!requirements.decals)
            m_state->decals.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::replace_targets(
        RenderState& state, Swapchain* swapchain, Math::Vec2u size) {
        if(swapchain) {
            const auto generation = swapchain->get_active_generation();
            if(!generation)
                return Result<void, GraphicsError>::failure({"No active presentation generation"});
            const auto extent = generation->get_config().extent;
            size = {extent.width, extent.height};
        }
        auto hdr = RenderTarget::try_create_multi_target(
            m_device, *state.scene_pass, state.quality.scene_size(size), m_frame_slot_count);
        if(!hdr)
            return Result<void, GraphicsError>::failure(hdr.error());
        std::shared_ptr<RenderTarget> output;
        if(swapchain) {
            auto candidate = RenderTarget::create_swapchain_target(
                m_device, state.output_pass->get_render_pass(), *swapchain);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            output = std::move(candidate).value();
        } else {
            auto candidate = RenderTarget::try_create_multi_target(
                m_device, state.output_pass->get_render_pass(), size, m_frame_slot_count);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            output = std::move(candidate).value();
        }
        std::unique_ptr<DepthPass> view_depth;
        if(state.requirements.needs_depth(m_post_process, bool(state.temporal))) {
            auto candidate =
                DepthPass::create(m_device, m_frame_slot_count, state.quality.scene_size(size), 1,
                    state.temporal ? DepthPass::Output::Motion : DepthPass::Output::Depth);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            view_depth = std::move(candidate).value();
        }
        std::shared_ptr<DeferredPass::Resources> deferred;
        if(state.deferred) {
            auto candidate = state.deferred->create_resources(state.quality.scene_size(size));
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            deferred = std::move(candidate).value();
        }
        std::unique_ptr<OutlinePass> outline;
        if(state.requirements.outline) {
            auto candidate =
                OutlinePass::create(m_device, m_frame_slot_count, state.quality.scene_size(size));
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            outline = std::move(candidate).value();
        }
        if(auto graph = rebuild_graph(state, m_post_process, state.requirements,
               {state.bloom.get(), state.fog.get(), state.reflections.get(), outline.get()},
               state.quality.scene_size(size));
            !graph)
            return graph;
        // 所有候选均成功后再发布，不改变在途帧保留的旧目标。
        if(view_depth)
            state.view_depth = std::move(view_depth);
        state.outline = std::move(outline);
        state.deferred_resources = std::move(deferred);
        state.hdr_target = std::move(hdr).value();
        state.output_target = std::move(output);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_presentation(
        RenderResources& resources, Swapchain& swapchain) {
        auto next = create_state(resources, &swapchain, {}, m_quality);
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_state = std::move(next).value();
        m_resize_failure.reset();
        m_post_process_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> SceneRenderer::configure_offscreen(
        RenderResources& resources, Math::Vec2u size) {
        auto next = create_state(resources, nullptr, size, m_quality);
        if(!next)
            return Result<void, GraphicsError>::failure(next.error());
        m_state = std::move(next).value();
        m_resize_failure.reset();
        m_post_process_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    Result<MaterialRenderer::ShaderUpdate, GraphicsError> SceneRenderer::prepare_material_shaders(
        const MaterialShaders& shaders) {
        if(!m_state)
            return Result<MaterialRenderer::ShaderUpdate, GraphicsError>::failure(
                {"Scene pipelines are not initialized"});
        return m_state->materials->prepare_shaders(
            *m_state->pipelines, shaders, static_cast<SampleCount>(m_quality.msaa_samples));
    }

    Result<void, GraphicsError> SceneRenderer::validate_quality_settings(
        const QualitySettings& settings) const {
        if(auto valid = settings.validate(); !valid)
            return Result<void, GraphicsError>::failure({valid.error()});
        const auto samples = static_cast<SampleCount>(settings.msaa_samples);
        if(auto color = validate_color_target(m_device.get_capability().physical_device,
               RenderSettings::SCENE_COLOR_FORMAT, samples);
            !color)
            return color;
        vk::ImageFormatProperties depth;
        const auto status = m_device.get_capability().physical_device.getImageFormatProperties(
            Graphics::format_to_vk(m_depth_format), vk::ImageType::e2D, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment, {}, &depth);
        if(status != vk::Result::eSuccess)
            return Result<void, GraphicsError>::failure(
                {"Cannot query depth target support", status});
        if(!(depth.sampleCounts & Graphics::sample_count_to_vk(samples)))
            return Result<void, GraphicsError>::failure(
                {"Depth target MSAA sample count is unsupported"});
        return Result<void, GraphicsError>::success();
    }

    Result<std::shared_ptr<SceneRenderer::RenderState>, GraphicsError> SceneRenderer::
        prepare_quality(
            RenderResources& resources, Swapchain& swapchain, const QualitySettings& settings) {
        return create_state(resources, is_offscreen() ? nullptr : &swapchain,
            m_state->output_target->get_size(), settings);
    }

    void SceneRenderer::install_quality(
        std::shared_ptr<RenderState> state, const QualitySettings& settings) {
        m_state = std::move(state);
        m_quality = settings;
        m_resize_failure.reset();
        m_post_process_failure.reset();
    }

    Math::Vec2u SceneRenderer::get_scene_size() const {
        return m_state->hdr_target->get_size();
    }

    std::vector<std::shared_ptr<const MaterialLayout>> SceneRenderer::get_material_layouts() const {
        if(!m_state)
            return {};
        return m_state->materials->get_material_layouts();
    }

    MaterialRenderer::Statistics SceneRenderer::get_material_statistics() const {
        return m_state->materials->get_statistics();
    }

    ShadowPass::Statistics SceneRenderer::get_shadow_statistics() const {
        return m_shadow->get_statistics();
    }

    PathTracePass::Statistics SceneRenderer::get_path_trace_statistics() const {
        return m_state->path_trace ? m_state->path_trace->statistics()
                                   : PathTracePass::Statistics{};
    }

    const PostProcessSettings& SceneRenderer::get_post_process_settings() const {
        return m_post_process;
    }

    RenderTarget& SceneRenderer::get_render_target() {
        return *m_state->output_target;
    }
    const RenderTarget& SceneRenderer::get_render_target() const {
        return *m_state->output_target;
    }

    bool SceneRenderer::is_offscreen() const {
        return m_state && m_state->offscreen;
    }

    MaterialRenderer& SceneRenderer::get_material_renderer() {
        return *m_state->materials;
    }

    void SceneRenderer::skip_frame() {
        if(m_state->temporal)
            m_state->temporal->reset();
        m_state->materials->reset_statistics();
        m_shadow->reset_statistics();
        m_state->particles->reset();
    }

    void SceneRenderer::remove_assets(const AssetRegistry::Removals& removals) {
        m_state->materials->remove_assets(removals);
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::render(
        FrameScheduler& frames, const RenderSubmission& source, const RenderGeometry& geometry,
        const LineDrawList& lines, RenderDiagnostics* diagnostics) {
        PROFILE_SCOPE("SceneRenderer::render");
        using RenderResult = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        auto submission = source;
        frames.retain_current_frame_resource(m_state);
        frames.retain_current_frame_resource(m_state->output_target);
        frames.retain_current_frame_resource(m_state->hdr_target);
        if(submission.path_trace && submission.view_project_matrix) {
            if(m_state->temporal)
                m_state->temporal->reset();
            m_state->materials->reset_statistics();
            m_shadow->reset_statistics();
            m_state->particles->reset();
            if(!m_state->path_trace)
                m_state->path_trace = std::make_unique<PathTracePass>(m_device);
            auto& trace = *m_state->path_trace;
            const auto display = [&](RenderGraph::PassId, CommandBuffer&) {
                PostProcessSettings settings;
                settings.exposure = m_post_process.exposure;
                return m_state->output_pass->render(
                    frames, m_state->output_target, trace.output(), settings);
            };
            if(auto drawn = trace.render(frames, submission.path_trace,
                   *submission.view_project_matrix, submission.path_trace_settings,
                   m_state->output_target->get_size(), display, diagnostics);
                !drawn)
                return RenderResult::failure(drawn.error());
            return RenderResult::success({});
        }
        if(m_state->path_trace)
            m_state->path_trace->reset();
        // 探针状态只显示本帧，关闭调试时也清除上一帧的标记历史。
        if(m_state->temporal
            && (submission.irradiance_debug != IrradianceDebugMode::Off
                || m_state->irradiance_debug != IrradianceDebugMode::Off))
            m_state->temporal->reset();
        m_state->irradiance_debug = submission.irradiance_debug;
        if(m_state->temporal) {
            if(!submission.view_project_matrix)
                m_state->temporal->reset();
            auto camera = m_state->temporal->prepare(frames, submission.world.scene_lifetime,
                submission.view_project_matrix.value_or(
                    ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)}),
                get_scene_size());
            if(!camera)
                return RenderResult::failure(camera.error());
            m_state->temporal_camera = camera.value();
            if(submission.view_project_matrix)
                submission.view_project_matrix = camera.value().current;
        }
        const auto image = frames.get_current_frame_slot_index();
        frames.retain_current_frame_resource(m_shadow);
        auto lighting = RenderDiagnostics::measure_preparation(diagnostics,
            RenderDiagnostics::PreparationPhase::Lighting,
            [&] { return ShadowPass::prepare(submission, geometry); });
        const bool shadows = ShadowPass::needs_shadow(lighting);
        const bool soft_particles =
            submission.world.particles && submission.world.particles->uses_soft_depth();
        const ViewRequirements requirements{shadows, soft_particles,
            !submission.world.decals.empty(),
            bool(submission.view_project_matrix)
                && submission.world.environment.volumetric_fog.active(),
            bool(submission.view_project_matrix)
                && std::ranges::any_of(submission.world.render_items,
                    [](const auto& item) { return item.outline.active(); }),
            bool(submission.view_project_matrix) && submission.world.particles
                && !submission.world.particles->gpu_emitters.empty()};
        if(requirements != m_state->requirements || (requirements.decals && !m_state->decals)) {
            if(auto rebuilt = configure_effects(m_post_process, requirements); !rebuilt)
                return RenderResult::failure(rebuilt.error());
        }
        if(auto prepared = m_shadow->prepare_frame(frames, lighting); !prepared)
            return RenderResult::failure(prepared.error());
        if(submission.view_project_matrix) {
            const auto& camera = *submission.view_project_matrix;
            lighting.inverse_view_projection = Math::inverse(camera.projection * camera.view);
            if(m_post_process.uses_ao())
                lighting.ao_parameters = {
                    m_post_process.ao_radius, m_post_process.ao_strength, 0, 0};
        }
        const ScopeExit clear_draws([&] {
            m_state->materials->clear_draws();
            m_state->view_geometry.clear_frame();
            m_state->particles->clear_draws();
        });
        // 主颜色与相机深度共用一次选级；各 Pass 仍保留自己的可见性和材质规则。
        const RenderGeometry* main_geometry = &geometry;
        if(submission.view_project_matrix) {
            const auto& camera = *submission.view_project_matrix;
            main_geometry =
                &m_state->view_geometry.prepare_view(geometry, camera.projection * camera.view);
        }
        m_state->fog_frame = {};
        if(m_state->requirements.fog)
            m_state->fog_frame = m_state->fog->prepare_frame(
                *m_state->fog_resources, image, submission, get_scene_size());
        auto prepared = m_state->materials->view_bindings().prepare(frames, submission, lighting,
            m_shadow->get_depth_view(image),
            m_state->view_depth_resource ? m_state->view_depth->get_depth_view(image) : nullptr,
            m_state->requirements.fog ? &m_state->fog_frame : nullptr);
        if(!prepared)
            return RenderResult::failure(prepared.error());
        if(auto draws = m_state->materials->prepare_draws(frames, submission, *main_geometry);
            !draws)
            return RenderResult::failure(draws.error());
        const auto& graph = m_state->graph;
        auto& bindings = m_state->bindings;
        bindings.resize(graph.resource_count());
        const ScopeExit release_bindings([&] { bindings.clear(); });
        bindings[m_state->shadow_resource.index] = m_shadow->get_depth_view(image)->get_image();
        const auto& attachments = m_state->hdr_target->get_framebuffer(image)->get_attachments();
        for(std::size_t index = 0; index < attachments.size(); ++index)
            bindings[m_state->scene_attachments[index].index] = attachments[index]->get_image();
        if(m_state->deferred) {
            const auto images = DeferredPass::images(*m_state->deferred_resources, image);
            for(std::size_t index = 0; index < images.size(); ++index)
                bindings[m_state->gbuffer_resources[index].index] = images[index]->get_image();
        }
        if(m_state->view_depth_resource)
            bindings[m_state->view_depth_resource->index] =
                m_state->view_depth->get_depth_view(image)->get_image();
        if(m_state->temporal) {
            bindings[m_state->motion_resource->index] =
                m_state->view_depth->get_motion_view(image)->get_image();
            bindings[m_state->temporal_output->index] = m_state->temporal->output()->get_image();
            bindings[m_state->temporal_history->index] = m_state->temporal->history()->get_image();
        }
        if(m_state->outline_mask_pass) {
            const auto images = m_state->outline->mask_images(image);
            for(size_t i = 0; i < images.size(); ++i)
                bindings[m_state->outline_masks[i].index] = images[i]->get_image();
        }
        m_state->transient_resources.bind_resources(bindings, image);
        auto waits = std::move(prepared).value();
        const auto recorder = [this, &frames, &submission, &geometry, &main_geometry, &lines,
                                  &lighting, &waits](RenderGraph::PassId pass, CommandBuffer&) {
            return record_pass(
                pass, frames, submission, geometry, *main_geometry, lines, lighting, waits);
        };
        auto recorded = Result<void, GraphicsError>::success();
        if(diagnostics)
            recorded = diagnostics->record(graph, bindings, recorder);
        else
            recorded = graph.record(frames, bindings, recorder);
        if(!recorded)
            return RenderResult::failure(recorded.error());
        if(diagnostics) {
            const auto shadow = get_shadow_statistics();
            diagnostics->record_draw_statistics(get_material_statistics().drawn_triangles,
                shadow.drawn_triangles, shadow.spot_lights, shadow.excess_spot_lights,
                shadow.point_lights, shadow.excess_point_lights);
        }
        return RenderResult::success(std::move(waits));
    }

    Result<void, GraphicsError> SceneRenderer::record_pass(std::size_t pass, FrameScheduler& frames,
        const RenderSubmission& submission, const RenderGeometry& geometry,
        const RenderGeometry& main_geometry, const LineDrawList& lines,
        const LightingData& lighting, std::vector<QueueSemaphoreSubmit>& waits) {
        const auto slot = frames.get_current_frame_slot_index();
        if(m_state->gbuffer_pass && pass == *m_state->gbuffer_pass) {
            m_state->deferred->record_geometry(
                frames, m_state->deferred_resources, *m_state->materials, waits);
            return Result<void, GraphicsError>::success();
        }
        if(m_state->fog_pass && pass == *m_state->fog_pass) {
            m_state->fog->render(frames, *m_state->fog_resources,
                m_state->materials->view_bindings().descriptor(slot));
            return Result<void, GraphicsError>::success();
        }
        const auto opaque_hdr = m_state->hdr_target->get_color_view(slot);
        const auto reflected_hdr =
            m_state->reflection_pass
                ? ScreenSpaceReflectionPass::output(*m_state->reflection_resources, slot)
                : opaque_hdr;
        if(m_state->reflection_pass && pass == *m_state->reflection_pass) {
            const auto attachments = m_state->hdr_target->get_framebuffer(slot)->get_attachments();
            return m_state->reflections->render(frames, m_state->reflection_resources,
                m_state->materials->view_bindings().descriptor(slot),
                DeferredPass::images(*m_state->deferred_resources, slot), opaque_hdr,
                attachments[1], m_post_process.reflections);
        }
        const auto resolved_hdr = m_state->temporal ? m_state->temporal->output() : reflected_hdr;
        const auto final_hdr =
            m_state->outline_resources ? m_state->outline_resources->images.at(slot) : resolved_hdr;
        if(m_state->gpu_particles_pass && pass == *m_state->gpu_particles_pass)
            return m_state->particles->prepare_gpu(
                frames, *submission.view_project_matrix, submission.world);
        if(m_state->outline_composite_pass && pass == *m_state->outline_composite_pass)
            return m_state->outline->composite(frames, m_state->outline_resources, resolved_hdr,
                m_state->view_depth->get_depth_view(slot), m_state->output_target->get_size());
        if(pass == m_state->output_pass_id) {
            const auto hdr = final_hdr;
            std::shared_ptr<ImageView> bloom;
            if(m_state->bloom_passes)
                bloom = m_state->bloom->get_output(slot);
            return m_state->output_pass->render(
                frames, m_state->output_target, hdr, m_post_process, bloom);
        }
        if(m_state->temporal_pass && pass == *m_state->temporal_pass)
            return m_state->temporal->render(frames, reflected_hdr,
                m_state->view_depth->get_depth_view(slot),
                m_state->view_depth->get_motion_view(slot));
        auto drawn = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::success({});
        if(m_state->outline_mask_pass && pass == *m_state->outline_mask_pass)
            drawn = m_state->outline->mask(frames, *submission.view_project_matrix, main_geometry);
        else if(m_state->view_depth_pass && pass == *m_state->view_depth_pass) {
            DepthPass::View view;
            view.enabled = bool(submission.view_project_matrix);
            if(view.enabled)
                view.view_projection = submission.view_project_matrix->projection
                                       * submission.view_project_matrix->view;
            if(m_state->temporal) {
                view.previous_view_projection = m_state->temporal_camera.previous;
                view.previous_projection = m_state->temporal_camera.previous_projection;
            }
            drawn = m_state->view_depth->render(frames, std::span(&view, 1), main_geometry,
                DepthPass::LodSelection::AlreadySelected);
        } else if(m_state->shadow_pass_id && pass == *m_state->shadow_pass_id) {
            drawn = m_shadow->render(frames, lighting, geometry);
        } else if(pass == m_state->scene_pass_id)
            drawn = draw_scene(frames, frames.get_current_command_buffer(), submission, lines);
        else if(m_state->bloom_passes)
            return m_state->bloom->render(
                frames, pass, *m_state->bloom_passes, final_hdr, m_post_process.bloom_threshold);
        else
            return Result<void, GraphicsError>::failure({"Unknown scene render pass"});
        if(!drawn)
            return Result<void, GraphicsError>::failure(drawn.error());
        for(const auto& wait : drawn.value())
            merge_semaphore_wait(waits, wait);
        return Result<void, GraphicsError>::success();
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SceneRenderer::draw_scene(
        FrameScheduler& frames, CommandBuffer& command, const RenderSubmission& submission,
        const LineDrawList& lines) {
        m_state->hdr_target->set_clear_value(
            ClearValue(Math::Vec4(submission.world.environment.background_color, 0.0f)));
        m_state->hdr_target->begin_render_target(command, frames.get_current_frame_slot_index());
        const auto size = m_state->hdr_target->get_size();
        command.set_viewport(
            Graphics::get_viewport(static_cast<float>(size.x), static_cast<float>(size.y)));
        command.set_scissor(
            Graphics::get_scissor(static_cast<float>(size.x), static_cast<float>(size.y)));
        auto skybox = m_state->skybox_pass->render(frames, submission);
        if(!skybox)
            return skybox;
        auto waits = std::move(skybox);
        if(m_state->requirements.fog)
            m_state->fog->render_background(frames,
                m_state->materials->view_bindings().descriptor(
                    frames.get_current_frame_slot_index()),
                size);
        if(submission.view_project_matrix) {
            auto particles = m_state->particles->prepare(frames, *submission.view_project_matrix,
                submission.world, !m_state->materials->transparent_depths().empty(),
                m_state->requirements.soft_particles
                    ? m_state->view_depth->get_depth_view(frames.get_current_frame_slot_index())
                    : nullptr,
                m_state->requirements.fog ? &m_state->fog_frame : nullptr);
            if(!particles)
                return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                    particles.error());
            for(const auto& wait : particles.value())
                merge_semaphore_wait(waits.value(), wait);
            if(m_state->deferred)
                m_state->deferred->record_lighting(frames, m_state->deferred_resources,
                    m_state->materials->view_bindings().descriptor(
                        frames.get_current_frame_slot_index()));
            m_state->materials->record_opaque(frames, waits.value());
            if(m_state->decals) {
                const auto slot = frames.get_current_frame_slot_index();
                auto drawn = m_state->decals->render(frames, submission,
                    m_state->materials->view_bindings().descriptor(slot),
                    m_state->view_depth->get_depth_view(slot), size);
                if(!drawn)
                    return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                        drawn.error());
                for(const auto& wait : drawn.value())
                    merge_semaphore_wait(waits.value(), wait);
            }
            record_transparent(frames, waits.value());
            if(submission.irradiance_debug != IrradianceDebugMode::Off) {
                const auto slot = frames.get_current_frame_slot_index();
                const auto& bindings = m_state->materials->view_bindings();
                if(auto probes = m_state->debug->render_irradiance(frames,
                       bindings.descriptor(slot), bindings.irradiance(slot), size,
                       submission.irradiance_debug, submission.realtime_irradiance);
                    !probes)
                    return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                        probes.error());
            }
            if(auto debug = m_state->debug->render(frames, *submission.view_project_matrix, lines);
                !debug)
                return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                    debug.error());
        } else
            m_state->particles->reset();
        m_state->hdr_target->end_render_target(command);
        std::erase_if(waits.value(),
            [](const auto& wait) { return wait.semaphore->has_reached(wait.value); });
        return waits;
    }

    void SceneRenderer::record_transparent(
        FrameScheduler& frames, std::vector<QueueSemaphoreSubmit>& waits) {
        const auto meshes = m_state->materials->transparent_depths();
        const auto particles = m_state->particles->depths();
        if(meshes.empty()) {
            m_state->particles->record(frames, 0, m_state->particles->instance_count());
            return;
        }
        if(particles.empty()) {
            m_state->materials->record_transparent(frames, 0, meshes.size(), waits);
            return;
        }
        // 两条有序深度流线性归并；连续同类区间保持批次，网格可切开粒子批次。
        size_t mesh = 0;
        uint32_t particle = 0;
        while(mesh < meshes.size() || particle < particles.size()) {
            if(particle == particles.size()
                || (mesh < meshes.size() && meshes[mesh] <= particles[particle])) {
                const auto first = mesh++;
                while(mesh < meshes.size()
                      && (particle == particles.size() || meshes[mesh] <= particles[particle]))
                    ++mesh;
                m_state->materials->record_transparent(frames, first, mesh - first, waits);
            } else {
                const auto first = particle++;
                while(particle < particles.size()
                      && (mesh == meshes.size() || particles[particle] < meshes[mesh]))
                    ++particle;
                m_state->particles->record(frames, first, particle - first);
            }
        }
    }

    Result<void, GraphicsError> SceneRenderer::resize_offscreen_target(
        const Math::Vec2u size, const std::chrono::steady_clock::time_point now) {
        if(!m_state->offscreen || size.x == 0 || size.y == 0) {
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && m_resize_failure->size != size)
            m_resize_failure.reset();
        if(m_state->output_target->get_size() == size) {
            m_resize_failure.reset();
            return Result<void, GraphicsError>::success();
        }
        if(m_resize_failure && !m_resize_failure->retry.consume(now))
            return Result<void, GraphicsError>::success();

        auto candidate = replace_targets(*m_state, nullptr, size);
        if(!candidate) {
            if(candidate.error().is_device_lost())
                return Result<void, GraphicsError>::failure(candidate.error());
            const Math::Vec2u current_size = m_state->output_target->get_size();
            if(!m_resize_failure)
                m_resize_failure = TargetRetry{size};
            auto& failure = *m_resize_failure;
            const auto attempts = failure.retry.retry_count() + 1;
            if(!candidate.error().is_out_of_memory() || !failure.retry.schedule(now)) {
                LOG_ERROR("Keeping offscreen target at {}x{}; resize to {}x{} stopped after {} "
                          "attempts, waiting for a new size: {}",
                    current_size.x, current_size.y, size.x, size.y, attempts,
                    candidate.error().message);
            } else if(attempts == 1) {
                LOG_WARN("Keeping offscreen target at {}x{}; resize to {}x{} will retry: {}",
                    current_size.x, current_size.y, size.x, size.y, candidate.error().message);
            }
            return Result<void, GraphicsError>::success();
        }

        LOG_INFO("Commit HDR and SDR target generation {}x{}", size.x, size.y);
        m_resize_failure.reset();
        return Result<void, GraphicsError>::success();
    }

    std::shared_ptr<ImageView> SceneRenderer::get_offscreen_color_view(uint32_t slot) const {
        if(!m_state || !m_state->offscreen)
            return nullptr;
        if(slot >= m_frame_slot_count)
            LOG_FATAL(
                "Offscreen frame slot {} exceeds frame slot count {}", slot, m_frame_slot_count);
        return m_state->output_target->get_color_view(slot);
    }

    std::shared_ptr<ImageView> SceneRenderer::get_hdr_color_view(uint32_t slot) const {
        if(!m_state)
            return nullptr;
        if(slot >= m_frame_slot_count)
            LOG_FATAL("HDR frame slot {} exceeds frame slot count {}", slot, m_frame_slot_count);
        if(m_state->path_trace && m_state->path_trace->statistics().samples != 0)
            return m_state->path_trace->output();
        if(m_state->temporal)
            if(auto resolved = m_state->temporal->output())
                return resolved;
        return m_state->hdr_target->get_color_view(slot);
    }

    void SceneRenderer::release_presentation_target() {
        if(m_state && !m_state->offscreen)
            m_state->output_target.reset();
    }

    Result<void, GraphicsError> SceneRenderer::rebuild_presentation_target(
        RenderResources& resources, Swapchain& swapchain,
        const SwapchainCompatibility& compatibility) {
        if(m_state->offscreen)
            return Result<void, GraphicsError>::success();
        if(compatibility.format_changed) {
            auto candidate = create_state(resources, &swapchain, {}, m_quality);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            m_state = std::move(candidate).value();
            return Result<void, GraphicsError>::success();
        }
        return replace_targets(*m_state, &swapchain, {});
    }

    Result<void, GraphicsError> SceneRenderer::configure_output_calibration(
        float headroom, float white_level) {
        if(auto applied = m_state->output_pass->configure_calibration(headroom, white_level);
            !applied)
            return applied;
        m_hdr_headroom = headroom;
        m_hdr_white_level = white_level;
        return Result<void, GraphicsError>::success();
    }
}
