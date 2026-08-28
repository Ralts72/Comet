#include "renderer.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/shader_types.h"
#include "graphics/pipeline.h"
#include "graphics/vertex_description.h"

namespace Comet {
    Renderer::Renderer(const Window& window,
                       const Config& config,
                       const AssetRegistry& asset_registry)
        : m_scene_resolver(asset_registry),
          m_msaa_samples(config.vulkan.msaa_samples) {
        PROFILE_SCOPE("Renderer::Constructor");

        // Create render context
        m_render_context = std::make_unique<RenderContext>(
            window, config.vulkan, config.render);

        // Create resource manager
        LOG_INFO("create resource manager");
        m_resource_manager = std::make_unique<ResourceManager>(
            m_render_context->get_device());

        // Create scene renderer
        LOG_INFO("create scene renderer");
        m_scene_renderer = std::make_unique<SceneRenderer>(
            *m_render_context, config.vulkan, config.render);

        // Setup render pass (moved to SceneRenderer)
        m_scene_renderer->setup_render_pass();

        // Setup pipeline
        setup_pipeline();
    }

    void Renderer::setup_pipeline() {
        // 创建 DescriptorSetLayout bindings
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));

        // 让 SceneRenderer 创建 DescriptorSetLayout
        auto descriptor_set_layout = m_scene_renderer->create_descriptor_set_layout(bindings);

        // 创建 ShaderLayout（包含 DescriptorSetLayout）
        ShaderLayout layout = {};
        layout.descriptor_set_layouts.push_back(descriptor_set_layout);
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex, 0, sizeof(PushConstant)));

        // 创建 VertexInputDescription
        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(0, sizeof(Math::Vertex), VertexInputRate::Vertex);
        vertex_input_description.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, position));
        vertex_input_description.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(Math::Vertex, texcoord));
        vertex_input_description.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, normal));

        // 创建 PipelineConfig
        PipelineConfig pipeline_config = {};
        pipeline_config.set_vertex_input_state(vertex_input_description);
        pipeline_config.set_input_assembly_state(Topology::TriangleList);

        pipeline_config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(m_msaa_samples, false, 0.2f);

        // 让 SceneRenderer 创建 Pipeline
        m_scene_renderer->setup_pipeline(
            *m_resource_manager, layout, pipeline_config);
    }

    void Renderer::on_render(const RenderScene& render_scene) {
        PROFILE_SCOPE("render frame");

        // Begin frame (acquires image and begins command buffer)
        if(!m_scene_renderer->begin_frame()) {
            return;
        }
        const RenderSubmission submission = m_scene_resolver.resolve(
            render_scene, m_scene_renderer->get_render_target().get_size());
        m_scene_renderer->render(submission);

        m_scene_renderer->end_render_pass();

        if(m_on_imgui_render) {
            m_on_imgui_render(m_scene_renderer->get_current_command_buffer());
        }

        // End frame (submits and presents)
        m_scene_renderer->end_frame();
    }

    void Renderer::enable_viewport_rendering(const Math::Vec2u initial_size) {
        m_render_context->wait_idle();
        m_scene_renderer->setup_viewport_render_pass(initial_size);
        setup_pipeline();
    }

    void Renderer::request_viewport_resize(const Math::Vec2u size) const {
        m_scene_renderer->request_viewport_resize(size);
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_render_context->wait_idle();

        // 清理子系统
        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
