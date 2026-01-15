#include "scene_renderer.h"
#include "common/logger.h"
#include "common/profiler.h"
#include "common/config.h"
#include "graphics/queue.h"
#include "graphics/image_view.h"
#include "graphics/vk_common.h"
#include "graphics/buffer.h"
#include "graphics/sampler.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "graphics/vertex_description.h"
#include "resource_manager.h"

namespace Comet {
    SceneRenderer::SceneRenderer(RenderContext* context)
        : m_context(context) {
        auto swapchain_image_count = Config::get<uint32_t>("vulkan.swapchain_image_count", 3);

        LOG_INFO("create frame manager");
        m_frame_manager = std::make_unique<FrameManager>(context->get_device(), swapchain_image_count);
    }

    void SceneRenderer::setup_render_pass() {
        LOG_INFO("create render pass");

        // 从配置文件读取设置
        const auto surface_format = static_cast<Format>(Config::get<int>("vulkan.surface_format", 44)); // B8G8R8A8_UNORM = 44
        const auto depth_format = static_cast<Format>(Config::get<int>("vulkan.depth_format", 126)); // D32_SFLOAT = 126
        const auto msaa_samples = static_cast<SampleCount>(Config::get<int>("vulkan.msaa_samples", 4)); // Count4 = 4

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

        m_render_pass = std::make_shared<RenderPass>(m_context->get_device(), attachments, render_sub_passes);

        LOG_INFO("create render pipeline manager");
        m_pipeline_manager = std::make_unique<PipelineManager>(m_context->get_device(), m_render_pass.get());

        LOG_INFO("create render target");
        m_render_target = RenderTarget::create_swapchain_target(
            m_context->get_device(), m_render_pass.get(), m_context->get_swapchain());

        // 从配置文件读取清除颜色
        const auto clear_color_array = Config::get<std::vector<float>>("render.clear_color", {0.2f, 0.4f, 0.1f, 1.0f});
        const Math::Vec4 clear_color(
            !clear_color_array.empty() ? clear_color_array[0] : 0.2f,
            clear_color_array.size() > 1 ? clear_color_array[1] : 0.4f,
            clear_color_array.size() > 2 ? clear_color_array[2] : 0.1f,
            clear_color_array.size() > 3 ? clear_color_array[3] : 1.0f
        );
        m_render_target->set_clear_value(ClearValue(clear_color));

        // Initialize command buffers
        m_frame_manager->initialize_command_buffers(m_context->get_swapchain()->get_images().size());
    }

    std::shared_ptr<DescriptorSetLayout> SceneRenderer::create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings) {
        if(!m_descriptor_set_layout) {
            m_descriptor_set_layout = std::make_shared<DescriptorSetLayout>(m_context->get_device(), bindings);
        }
        return m_descriptor_set_layout;
    }

    void SceneRenderer::setup_pipeline(ResourceManager* resource_manager,
                                       const ShaderLayout& layout,
                                       const VertexInputDescription& vertex_input,
                                       const PipelineConfig& config) {
        LOG_INFO("setup pipeline");

        // 创建着色器
        auto vert_shader = resource_manager->get_shader_manager()->load_shader("cube_texture_vert", CUBE_TEXTURE_VERT, layout);
        auto frag_shader = resource_manager->get_shader_manager()->load_shader("cube_texture_frag", CUBE_TEXTURE_FRAG, layout);

        // 创建 Pipeline
        m_pipeline = m_pipeline_manager->create_pipeline(
            "cube_pipeline", layout, vertex_input, config, vert_shader, frag_shader);
    }

    void SceneRenderer::setup_descriptor_sets(const DescriptorSetLayoutBindings& bindings) {
        LOG_INFO("create descriptor pool and descriptor sets");

        // 如果 DescriptorSetLayout 还没有创建，先创建它
        if(!m_descriptor_set_layout) {
            m_descriptor_set_layout = std::make_shared<DescriptorSetLayout>(m_context->get_device(), bindings);
        }

        DescriptorPoolSizes descriptor_pool_sizes;
        descriptor_pool_sizes.add_pool_size(DescriptorType::UniformBuffer, 2);
        descriptor_pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler, 2);
        m_descriptor_pool = std::make_shared<DescriptorPool>(m_context->get_device(), 1, descriptor_pool_sizes);
        m_descriptor_sets = m_descriptor_pool->allocate_descriptor_set(*m_descriptor_set_layout, 1);
    }

    uint32_t SceneRenderer::begin_frame() {
        PROFILE_SCOPE("SceneRenderer::begin_frame");
        m_frame_manager->begin_frame();

        auto swapchain = m_context->get_swapchain();
        auto& frame_sync = m_frame_manager->get_current_sync();
        auto& wait_sem = frame_sync.image_semaphore;

        // Acquire next image
        auto [image_index, acquire_result] = swapchain->acquire_next_image(wait_sem);
        if(acquire_result == vk::Result::eErrorOutOfDateKHR) {
            recreate_swapchain();
            std::tie(image_index, acquire_result) = swapchain->acquire_next_image(wait_sem);
            if(acquire_result != vk::Result::eSuccess && acquire_result != vk::Result::eSuboptimalKHR) {
                LOG_FATAL("can't acquire swapchain image");
            }
        }

        auto& command_buffer = m_frame_manager->get_command_buffer(image_index);
        command_buffer.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        m_render_target->begin_render_target(command_buffer);

        return image_index;
    }

    void SceneRenderer::render(const ViewProjectMatrix& view_project, const ModelMatrix& model,
                               std::shared_ptr<Mesh> mesh,
                               const std::vector<DescriptorSet>& descriptor_sets) const {
        PROFILE_SCOPE("SceneRenderer::render");

        if(!m_pipeline) {
            LOG_ERROR("Pipeline not set up. Call setup_pipeline() first.");
            return;
        }

        auto swapchain = m_context->get_swapchain();
        auto image_index = swapchain->get_current_index();
        auto& command_buffer = m_frame_manager->get_command_buffer(image_index);

        // Bind pipeline
        command_buffer.bind_pipeline(*m_pipeline);

        // Set dynamic states
        const auto viewport = Graphics::get_viewport(
            static_cast<float>(swapchain->get_width()),
            static_cast<float>(swapchain->get_height()));
        command_buffer.set_viewport(viewport);

        const auto scissor = Graphics::get_scissor(
            static_cast<float>(swapchain->get_width()),
            static_cast<float>(swapchain->get_height()));
        command_buffer.set_scissor(scissor);

        // Bind descriptor sets
        std::vector<vk::DescriptorSet> vk_descriptor_sets;
        vk_descriptor_sets.reserve(descriptor_sets.size());
        for(auto ds: descriptor_sets) {
            vk_descriptor_sets.push_back(ds.get());
        }
        command_buffer.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            m_pipeline->get_layout()->get(), 0, static_cast<uint32_t>(vk_descriptor_sets.size()),
            vk_descriptor_sets.data(), 0, nullptr);

        // Draw
        mesh->draw(command_buffer);
    }

    void SceneRenderer::end_frame() {
        PROFILE_SCOPE("SceneRenderer::end_frame");

        auto device = m_context->get_device();
        auto swapchain = m_context->get_swapchain();
        auto image_index = swapchain->get_current_index();
        auto& command_buffer = m_frame_manager->get_command_buffer(image_index);
        auto& frame_sync = m_frame_manager->get_current_sync();

        // End render pass
        m_render_target->end_render_target(command_buffer);

        // End command buffer
        command_buffer.end();

        // Submit
        auto& graphics_queue = device->get_graphics_queue(0);
        graphics_queue.submit(std::span(&command_buffer, 1),
            std::span(&frame_sync.image_semaphore, 1),
            std::span(&frame_sync.submit_semaphore, 1),
            &frame_sync.fence);

        // Present
        auto& present_queue = device->get_present_queue(0);
        const auto result = present_queue.present(*swapchain,
            std::span(&frame_sync.submit_semaphore, 1), image_index);
        if(result == vk::Result::eSuboptimalKHR) {
            recreate_swapchain();
        }

        m_frame_manager->end_frame();
    }

    CommandBuffer& SceneRenderer::get_current_command_buffer() const {
        const auto image_index = m_context->get_swapchain()->get_current_index();
        return m_frame_manager->get_command_buffer(image_index);
    }

    void SceneRenderer::recreate_swapchain() {
        PROFILE_SCOPE("SceneRenderer::recreate_swapchain");
        auto device = m_context->get_device();
        auto swapchain = m_context->get_swapchain();

        device->wait_idle();
        const auto original_size = Math::Vec2u(swapchain->get_width(), swapchain->get_height());
        const bool flag = swapchain->recreate();
        const auto size = Math::Vec2u(swapchain->get_width(), swapchain->get_height());
        if(flag && original_size != size) {
            m_render_target->resize(size.x, size.y);
        }

        // Recreate render target with new swapchain
        m_render_target = RenderTarget::create_swapchain_target(
            m_context->get_device(), m_render_pass.get(), m_context->get_swapchain());

        // 重新设置清除颜色
        const auto clear_color_array = Config::get<std::vector<float>>("render.clear_color", {0.2f, 0.4f, 0.1f, 1.0f});
        const Math::Vec4 clear_color(
            !clear_color_array.empty() ? clear_color_array[0] : 0.2f,
            clear_color_array.size() > 1 ? clear_color_array[1] : 0.4f,
            clear_color_array.size() > 2 ? clear_color_array[2] : 0.1f,
            clear_color_array.size() > 3 ? clear_color_array[3] : 1.0f
        );
        m_render_target->set_clear_value(ClearValue(clear_color));

        // Reinitialize command buffers
        m_frame_manager->initialize_command_buffers(swapchain->get_images().size());
    }

    void SceneRenderer::update_descriptor_sets(const std::vector<DescriptorSet>& descriptor_sets,
                                               std::shared_ptr<Buffer> view_project_buffer,
                                               std::shared_ptr<Buffer> model_buffer,
                                               std::shared_ptr<Texture> texture1,
                                               std::shared_ptr<Texture> texture2,
                                               SamplerManager* sampler_manager) {
        vk::DescriptorBufferInfo buffer_info1{};
        buffer_info1.buffer = view_project_buffer->get();
        buffer_info1.offset = 0;
        buffer_info1.range = sizeof(ViewProjectMatrix);

        vk::DescriptorBufferInfo buffer_info2{};
        buffer_info2.buffer = model_buffer->get();
        buffer_info2.offset = 0;
        buffer_info2.range = sizeof(ModelMatrix);

        vk::DescriptorImageInfo image_info1{};
        image_info1.sampler = sampler_manager->get_linear_repeat()->get();
        image_info1.imageView = texture1->get_image_view()->get();
        image_info1.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo image_info2{};
        image_info2.sampler = sampler_manager->get_linear_repeat()->get();
        image_info2.imageView = texture2->get_image_view()->get();
        image_info2.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        DescriptorSet set = descriptor_sets[0];
        std::vector<vk::WriteDescriptorSet> write_sets;

        vk::WriteDescriptorSet set1{};
        set1.dstSet = set.get();
        set1.dstBinding = 0;
        set1.dstArrayElement = 0;
        set1.descriptorType = vk::DescriptorType::eUniformBuffer;
        set1.descriptorCount = 1;
        set1.pBufferInfo = &buffer_info1;
        write_sets.emplace_back(set1);

        vk::WriteDescriptorSet set2{};
        set2.dstSet = set.get();
        set2.dstBinding = 1;
        set2.dstArrayElement = 0;
        set2.descriptorType = vk::DescriptorType::eUniformBuffer;
        set2.descriptorCount = 1;
        set2.pBufferInfo = &buffer_info2;
        write_sets.emplace_back(set2);

        vk::WriteDescriptorSet set3{};
        set3.dstSet = set.get();
        set3.dstBinding = 2;
        set3.dstArrayElement = 0;
        set3.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        set3.descriptorCount = 1;
        set3.pImageInfo = &image_info1;
        write_sets.emplace_back(set3);

        vk::WriteDescriptorSet set4{};
        set4.dstSet = set.get();
        set4.dstBinding = 3;
        set4.dstArrayElement = 0;
        set4.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        set4.descriptorCount = 1;
        set4.pImageInfo = &image_info2;
        write_sets.emplace_back(set4);

        m_context->get_device()->get().updateDescriptorSets(write_sets, {});
    }
}

