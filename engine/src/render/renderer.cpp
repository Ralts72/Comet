#include "renderer.h"
#include "asset/registry.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "graphics/buffer.h"
#include "graphics/pipeline.h"
#include "graphics/vertex_description.h"
#include "material.h"

#include <string_view>
#include <variant>

namespace Comet {
    namespace {
        std::shared_ptr<Texture> get_texture_property(
            const Material& material, const std::string_view property_name) {
            const std::string name(property_name);
            if(!material.has_property(name)) {
                return nullptr;
            }

            const MaterialProperty& property = material.get_property(name);
            if(property.type != MaterialPropertyType::Texture) {
                return nullptr;
            }

            const auto* texture = std::get_if<std::shared_ptr<Texture>>(&property.value);
            return texture ? *texture : nullptr;
        }
    }

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

        // Setup pipeline
        setup_pipeline();

        // Setup per-frame render resources
        setup_resources();
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

        auto msaa_samples = static_cast<SampleCount>(m_vulkan_config.msaa_samples);

        pipeline_config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        pipeline_config.enable_depth_test();
        pipeline_config.set_multisample_state(msaa_samples, false, 0.2f);

        // 让 SceneRenderer 创建 Pipeline
        m_scene_renderer->setup_pipeline(m_resource_manager.get(), layout, vertex_input_description, pipeline_config);
    }

    void Renderer::setup_resources() {
        LOG_INFO("create uniform buffers");
        const uint32_t frame_slot_count =
            m_scene_renderer->get_frame_manager()->get_frame_slot_count();
        m_view_project_uniform_buffers.reserve(frame_slot_count);
        for(uint32_t i = 0; i < frame_slot_count; ++i) {
            m_view_project_uniform_buffers.push_back(Buffer::create_cpu_buffer(
                m_render_context->get_device(), Flags<BufferUsage>(BufferUsage::Uniform),
                sizeof(ViewProjectMatrix), nullptr));
        }
    }

    void Renderer::on_update([[maybe_unused]] const float delta_time) {
        PROFILE_SCOPE("render update");

        const auto swapchain = m_render_context->get_swapchain();
        m_view_project_matrix.view = Math::look_at(
            Math::Vec3(0.0f, 0.0f, 3.0f),
            Math::Vec3(0.0f, 0.0f, 0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f));
        m_view_project_matrix.projection = Math::perspective(45.0f,
            static_cast<float>(swapchain->get_width()) / static_cast<float>(swapchain->get_height()), 0.1f, 100.0f);
    }

    void Renderer::on_render(const RenderScene& render_scene, const AssetRegistry& asset_registry) {
        PROFILE_SCOPE("render frame");

        // Begin frame (acquires image and begins command buffer)
        m_scene_renderer->begin_frame();
        const uint32_t frame_slot_index =
            m_scene_renderer->get_frame_manager()->get_current_frame_slot_index();
        const auto& view_project_buffer =
            m_view_project_uniform_buffers.at(frame_slot_index);

        static_pointer_cast<CPUBuffer>(view_project_buffer)->write(&m_view_project_matrix);

        for(const RenderItem& render_item : render_scene.render_items) {
            const auto mesh = asset_registry.resolve<Mesh>(render_item.mesh_handle);
            if(!mesh) {
                if(m_missing_mesh_handles.insert(render_item.mesh_handle).second) {
                    LOG_ERROR("Render item references missing mesh handle {}",
                        render_item.mesh_handle.value());
                }
                continue;
            }
            m_missing_mesh_handles.erase(render_item.mesh_handle);

            const auto material = asset_registry.resolve<Material>(render_item.material_handle);
            if(!material) {
                if(m_missing_material_handles.insert(render_item.material_handle).second) {
                    LOG_ERROR("Render item references missing material handle {}",
                        render_item.material_handle.value());
                }
                continue;
            }
            m_missing_material_handles.erase(render_item.material_handle);

            const auto texture0 = get_texture_property(*material, "u_Texture0");
            const auto texture1 = get_texture_property(*material, "u_Texture1");
            if(!texture0 || !texture1) {
                if(m_invalid_material_handles.insert(render_item.material_handle).second) {
                    LOG_ERROR(
                        "Material handle {} requires Texture properties u_Texture0 and u_Texture1",
                        render_item.material_handle.value());
                }
                continue;
            }
            m_invalid_material_handles.erase(render_item.material_handle);

            const DescriptorSet& descriptor_set =
                m_scene_renderer->prepare_material_descriptor_set(
                    render_item.material_handle,
                    view_project_buffer,
                    texture0,
                    texture1,
                    m_resource_manager->get_sampler_manager());
            m_scene_renderer->render_item(
                render_item.model_matrix, mesh, descriptor_set);
        }

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

        m_view_project_uniform_buffers.clear();

        // 清理子系统
        m_scene_renderer.reset();
        m_resource_manager.reset();
        m_render_context.reset();
    }
}
