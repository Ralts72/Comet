#include "renderer.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "graphics/vertex_description.h"
#include "common/geometry_utils.h"
#include "graphics/buffer.h"
#include "graphics/pipeline.h"
#include "common/shader_resources.h"

namespace Comet {
    float Renderer::total_time = 0.0f;

    Renderer::Renderer(const Window& window, const Config::Runtime& config)
        : m_vulkan_config(config.vulkan), m_render_config(config.render) {
        PROFILE_SCOPE("Renderer::Constructor");

        // Create render context
        m_render_context = std::make_unique<RenderContext>(window, m_vulkan_config, m_render_config);

        // Create resource manager
        LOG_INFO("create resource manager");
        m_resource_manager = std::make_unique<ResourceManager>(m_render_context->get_device());

        // Create scene renderer
        LOG_INFO("create scene renderer");
        m_scene_renderer = std::make_unique<SceneRenderer>(m_render_context.get(), m_vulkan_config, m_render_config);

        // Setup render pass (moved to SceneRenderer)
        m_scene_renderer->setup_render_pass();

        // Setup pipeline and descriptor sets
        setup_pipeline();
        setup_descriptor_sets();

        // Setup application resources
        setup_resources();
    }

    void Renderer::setup_pipeline() {
        // 创建 DescriptorSetLayout bindings
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));

        // 让 SceneRenderer 创建 DescriptorSetLayout
        auto descriptor_set_layout = m_scene_renderer->create_descriptor_set_layout(bindings);

        // 创建 ShaderLayout（包含 DescriptorSetLayout）
        ShaderLayout layout = {};
        layout.descriptor_set_layouts.push_back(descriptor_set_layout);

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

        auto msaa_samples = static_cast<SampleCount>(m_vulkan_config.msaa_samples);

        pipeline_config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(msaa_samples, false, 0.2f);

        // 让 SceneRenderer 创建 Pipeline
        m_scene_renderer->setup_pipeline(m_resource_manager.get(), layout, vertex_input_description, pipeline_config);
    }

    void Renderer::setup_descriptor_sets() const {
        // 创建 DescriptorSetLayout bindings
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));

        // 让 SceneRenderer 创建 DescriptorSet
        m_scene_renderer->setup_descriptor_sets(bindings);
    }

    void Renderer::setup_resources() {
        LOG_INFO("create uniform buffers");
        const uint32_t frame_slot_count =
            m_scene_renderer->get_frame_manager()->get_frame_slot_count();
        m_view_project_uniform_buffers.reserve(frame_slot_count);
        m_model_uniform_buffers.reserve(frame_slot_count);
        for(uint32_t i = 0; i < frame_slot_count; ++i) {
            m_view_project_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                m_render_context->get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ViewProjectMatrix), nullptr));
            m_model_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                m_render_context->get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ModelMatrix), nullptr));
        }

        LOG_INFO("load textures");
        std::string image_path = std::string(PROJECT_ROOT_DIR) + "/engine/assets/textures/";
        m_texture1 = m_resource_manager->load_texture(image_path + "awesomeface.png");
        m_texture2 = m_resource_manager->load_texture(image_path + "R-C.jpeg");

        LOG_INFO("create mesh");
        auto [cube_vertices, cube_indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
        m_cube_mesh = m_resource_manager->create_mesh("demo_cube", cube_vertices, cube_indices);
    }

    void Renderer::on_update(const float delta_time) {
        PROFILE_SCOPE("render update");
        total_time += delta_time;

        m_model_matrix.model = Math::rotate(Math::Mat4(1.0f), Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        m_model_matrix.model = Math::rotate(m_model_matrix.model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        const auto swapchain = m_render_context->get_swapchain();
        m_view_project_matrix.view = Math::look_at(
            Math::Vec3(0.0f, 0.0f, 3.0f),
            Math::Vec3(0.0f, 0.0f, 0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));
        m_view_project_matrix.projection = Math::perspective(45.0f,
            static_cast<float>(swapchain->get_width()) / static_cast<float>(swapchain->get_height()), 0.1f, 100.0f);
    }

    void Renderer::on_render() const {
        PROFILE_SCOPE("render frame");

        // Begin frame (acquires image and begins command buffer)
        m_scene_renderer->begin_frame();
        const uint32_t frame_slot_index =
            m_scene_renderer->get_frame_manager()->get_current_frame_slot_index();
        const auto& view_project_buffer =
            m_view_project_uniform_buffers.at(frame_slot_index);
        const auto& model_buffer =
            m_model_uniform_buffers.at(frame_slot_index);
        const auto& descriptor_set =
            m_scene_renderer->get_descriptor_sets().at(frame_slot_index);

        // Update uniform buffers
        static_pointer_cast<CPUBuffer>(view_project_buffer)->write(&m_view_project_matrix);
        static_pointer_cast<CPUBuffer>(model_buffer)->write(&m_model_matrix);

        // Update this frame's descriptor set after its fence has completed.
        m_scene_renderer->update_descriptor_set(descriptor_set,
            view_project_buffer, model_buffer,
            m_texture1, m_texture2,
            m_resource_manager->get_sampler_manager());

        // Render
        m_scene_renderer->render(m_view_project_matrix, m_model_matrix,
            m_cube_mesh, descriptor_set);

        m_scene_renderer->end_render_pass();

        if(m_on_imgui_render) {
            m_on_imgui_render(m_scene_renderer->get_current_command_buffer());
        }

        // End frame (submits and presents)
        m_scene_renderer->end_frame();
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        m_render_context->wait_idle();

        // 清理应用层资源
        m_view_project_uniform_buffers.clear();
        m_model_uniform_buffers.clear();
        m_texture1.reset();
        m_texture2.reset();
        m_cube_mesh.reset();

        // 清理子系统
        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
