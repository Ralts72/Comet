#include "renderer.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/config.h"
#include "graphics/vertex_description.h"
#include "common/geometry_utils.h"
#include "graphics/image_view.h"
#include "graphics/buffer.h"
#include "graphics/pipeline.h"
#include "common/shader_resources.h"

namespace Comet {
    float Renderer::total_time = 0.0f;

    Renderer::Renderer(const Window& window) {
        PROFILE_SCOPE("Renderer::Constructor");
        LOG_INFO("init graphics system");

        // Create render context
        m_render_context = std::make_unique<RenderContext>(window);

        // Create resource manager
        LOG_INFO("create resource manager");
        m_resource_manager = std::make_unique<ResourceManager>(m_render_context->get_device());

        // Create render pass
        create_render_pass();

        // Create scene renderer
        LOG_INFO("create scene renderer");
        m_scene_renderer = std::make_unique<SceneRenderer>(
            m_render_context.get(), m_resource_manager.get(), m_render_pass.get());

        // Setup pipeline and resources
        setup_pipeline();
        setup_descriptor_sets();
        setup_resources();
    }

    void Renderer::create_render_pass() {
        LOG_INFO("create render pass");

        // 从配置文件读取设置
        Format surface_format = static_cast<Format>(Config::get<int>("vulkan.surface_format", 44)); // B8G8R8A8_UNORM = 44
        Format depth_format = static_cast<Format>(Config::get<int>("vulkan.depth_format", 126)); // D32_SFLOAT = 126
        SampleCount msaa_samples = static_cast<SampleCount>(Config::get<int>("vulkan.msaa_samples", 4)); // Count4 = 4

        std::vector<Attachment> attachments;
        attachments.emplace_back(Attachment::get_color_attachment(surface_format, msaa_samples));
        attachments.emplace_back(Attachment::get_depth_attachment(depth_format, msaa_samples));

        std::vector<RenderSubPass> render_sub_passes;
        RenderSubPass render_sub_pass_0 = {
            {},
            {SubpassColorAttachment(0)},
            {SubpassDepthStencilAttachment(1)},
            msaa_samples
        };
        render_sub_passes.emplace_back(render_sub_pass_0);

        m_render_pass = std::make_shared<RenderPass>(m_render_context->get_device(), attachments, render_sub_passes);
    }

    void Renderer::setup_pipeline() {
        LOG_INFO("setup pipeline");
        ShaderLayout layout = {};
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        bindings.add_binding(2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(3, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        m_descriptor_set_layout = std::make_shared<DescriptorSetLayout>(m_render_context->get_device(), bindings);
        layout.descriptor_set_layouts.push_back(m_descriptor_set_layout);

        auto vert_shader = m_resource_manager->get_shader_manager()->load_shader("cube_texture_vert", CUBE_TEXTURE_VERT, layout);
        auto frag_shader = m_resource_manager->get_shader_manager()->load_shader("cube_texture_frag", CUBE_TEXTURE_FRAG, layout);

        VertexInputDescription vertex_input_description;
        vertex_input_description.add_binding(0, sizeof(Math::Vertex), VertexInputRate::Vertex);
        vertex_input_description.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, position));
        vertex_input_description.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(Math::Vertex, texcoord));
        vertex_input_description.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(Math::Vertex, normal));

        PipelineConfig pipeline_config = {};
        pipeline_config.set_vertex_input_state(vertex_input_description);
        pipeline_config.set_input_assembly_state(Topology::TriangleList);

        // 从配置文件读取 MSAA 设置
        SampleCount msaa_samples = static_cast<SampleCount>(Config::get<int>("vulkan.msaa_samples", 4)); // Count4 = 4

        pipeline_config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(msaa_samples, false, 0.2f);

        m_pipeline = m_scene_renderer->get_pipeline_manager()->create_pipeline(
            "cube_pipeline", layout, vertex_input_description, pipeline_config, vert_shader, frag_shader);
    }

    void Renderer::setup_descriptor_sets() {
        LOG_INFO("create descriptor pool and descriptor sets");
        DescriptorPoolSizes descriptor_pool_sizes;
        descriptor_pool_sizes.add_pool_size(DescriptorType::UniformBuffer, 2);
        descriptor_pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler, 2);
        m_descriptor_pool = std::make_shared<DescriptorPool>(m_render_context->get_device(), 1, descriptor_pool_sizes);
        m_descriptor_sets = m_descriptor_pool->allocate_descriptor_set(*m_descriptor_set_layout, 1);
    }

    void Renderer::setup_resources() {
        LOG_INFO("create uniform buffers");
        m_view_project_uniform_buffer = Buffer::create_cpu_buffer(
            m_render_context->get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
        sizeof(ViewProjectMatrix), nullptr);
        m_model_uniform_buffer = Buffer::create_cpu_buffer(
            m_render_context->get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
        sizeof(ModelMatrix), nullptr);

        LOG_INFO("load textures");
        std::string image_path = std::string(PROJECT_ROOT_DIR) + "/engine/assets/textures/";
        m_texture1 = m_resource_manager->load_texture(image_path + "awesomeface.png");
        m_texture2 = m_resource_manager->load_texture(image_path + "R-C.jpeg");

        LOG_INFO("create mesh");
        auto [cube_vertices, cube_indices] = GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
        m_cube_mesh = m_resource_manager->create_mesh(cube_vertices, cube_indices);
    }

    void Renderer::on_update(const float delta_time) {
        PROFILE_SCOPE("render update");
        total_time += delta_time;

        m_model_matrix.model = Math::rotate(Math::Mat4(1.0f), Math::radians(-17.0f), Math::Vec3(1.0f, 0.0f, 0.0f));
        m_model_matrix.model = Math::rotate(m_model_matrix.model, Math::radians(total_time * 100.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        auto swapchain = m_render_context->get_swapchain();
        m_view_project_matrix.view = Math::look_at(Math::Vec3(0.0f, 0.0f, 30.5f),
            Math::Vec3(0.0f, 0.0f, -10.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));
        m_view_project_matrix.projection = Math::perspective(Math::radians(45.0f),
            static_cast<float>(swapchain->get_width()) / static_cast<float>(swapchain->get_height()), 0.1f, 100.0f);
    }

    void Renderer::on_render() {
        PROFILE_SCOPE("render frame");

        // Begin frame (acquires image and begins command buffer)
        m_scene_renderer->begin_frame();

        // Update uniform buffers
        static_pointer_cast<CPUBuffer>(m_view_project_uniform_buffer)->write(&m_view_project_matrix);
        static_pointer_cast<CPUBuffer>(m_model_uniform_buffer)->write(&m_model_matrix);

        // Update descriptor sets
        m_scene_renderer->update_descriptor_sets(m_descriptor_sets,
            m_view_project_uniform_buffer, m_model_uniform_buffer,
            m_texture1, m_texture2,
            m_resource_manager->get_sampler_manager());

        // Render
        m_scene_renderer->render(m_view_project_matrix, m_model_matrix,
            m_cube_mesh, m_pipeline, m_descriptor_sets);

        // End frame (submits and presents)
        m_scene_renderer->end_frame();
    }

    Renderer::~Renderer() {
        LOG_INFO("destroy renderer");
        auto device = m_render_context->get_device();
        if (device) {
            device->wait_idle();
        }

        m_view_project_uniform_buffer.reset();
        m_model_uniform_buffer.reset();
        m_texture1.reset();
        m_texture2.reset();
        m_cube_mesh.reset();
        m_descriptor_pool.reset();
        m_descriptor_set_layout.reset();
        m_pipeline.reset();
        m_scene_renderer.reset();
        m_render_pass.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
