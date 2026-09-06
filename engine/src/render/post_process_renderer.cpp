#include "render/post_process_renderer.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "fullscreen_vert.h"
#include "tone_map_frag.h"

#include <cmath>
#include <stdexcept>

namespace Comet {
    struct PostProcessRenderer::Binding {
        std::shared_ptr<ImageView> image;
        std::shared_ptr<DescriptorPool> pool;
        DescriptorSet descriptor;
    };

    PostProcessRenderer::PostProcessRenderer(Device& device, const Format output_format,
        const bool offscreen, const uint32_t frame_slots)
        : m_device(device), m_bindings(frame_slots), m_encode_srgb(false) {
        switch(output_format) {
            case Format::R8G8B8A8_SRGB:
            case Format::B8G8R8A8_SRGB:
                break;
            case Format::R8G8B8A8_UNORM:
            case Format::B8G8R8A8_UNORM:
                m_encode_srgb = true;
                break;
            default:
                throw std::invalid_argument("Post process requires an SDR RGBA output");
        }
        if(frame_slots == 0)
            throw std::invalid_argument("Post process requires frame slots");
        auto color = Attachment::get_color_attachment(output_format);
        color.description.store_op = AttachmentStoreOp::Store;
        if(offscreen) {
            color.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color.usage |= ImageUsage::Sampled;
            color.usage |= ImageUsage::CopySrc;
        }
        m_render_pass =
            std::make_shared<RenderPass>(device, std::vector<Attachment>{color},
                std::vector<RenderSubPass>{
                    {{}, {SubpassColorAttachment(0)}, {}, SampleCount::Count1}},
                output_format);

        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        m_layout = std::make_shared<DescriptorSetLayout>(device, bindings);
        m_sampler = Sampler::create_nearest_clamp(device);
        ShaderLayout layout;
        layout.descriptor_set_layouts = {m_layout};
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 8));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *m_render_pass);
        m_pipeline = pipelines.create_pipeline("tone_map", layout, config,
            std::make_shared<Shader>(device, "fullscreen", FULLSCREEN_VERT),
            std::make_shared<Shader>(device, "tone_map", TONE_MAP_FRAG));
    }

    void PostProcessRenderer::render(FrameScheduler& frames,
        const std::shared_ptr<RenderTarget>& output, const uint32_t output_index,
        const std::shared_ptr<ImageView>& hdr_color, const float exposure) {
        if(!frames.is_recording_frame() || !output || !hdr_color
            || !std::isfinite(exposure) || exposure < 0.0f)
            throw std::invalid_argument("Invalid post process frame, input or exposure");
        auto& binding = m_bindings.at(frames.get_current_frame_slot_index());
        if(!binding || binding->image != hdr_color) {
            DescriptorPoolSizes sizes;
            sizes.add_pool_size(DescriptorType::CombinedImageSampler, 1);
            auto pool = std::make_shared<DescriptorPool>(m_device, 1, sizes);
            const auto descriptor = pool->allocate_descriptor_set(*m_layout, 1).front();
            auto candidate =
                std::make_shared<Binding>(Binding{hdr_color, pool, descriptor});
            const vk::DescriptorImageInfo image(m_sampler->get(), hdr_color->get(),
                vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet write;
            write.dstSet = descriptor.get();
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &image;
            m_device.get().updateDescriptorSets(write, {});
            binding = std::move(candidate);
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_layout);
        frames.retain_current_frame_resource(m_sampler);
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(output);
        auto& command = frames.get_current_command_buffer();
        output->begin_render_target(command, output_index);
        const auto size = output->get_size();
        // 正高度 viewport 保持输入纹理与输出 framebuffer 的像素方向一致。
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_pipeline);
        command.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            m_pipeline->get_layout()->get(), 0, binding->descriptor.get(), {});
        struct Parameters {
            float exposure;
            uint32_t encode_srgb;
        };
        const Parameters parameters{exposure, m_encode_srgb ? 1u : 0u};
        command.push_constants(*m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Fragment), 0, &parameters,
            sizeof(parameters));
        command.draw(3);
        output->end_render_target(command);
    }
}
