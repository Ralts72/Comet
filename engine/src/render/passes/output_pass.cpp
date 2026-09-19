#include "render/passes/output_pass.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "display_vert.h"
#include "display_frag.h"

#include <cmath>
#include <utility>

namespace Comet {
    struct OutputPass::Binding {
        std::shared_ptr<ImageView> image;
        std::unique_ptr<DescriptorPool> pool;
        DescriptorSet descriptor;
    };

    OutputPass::OutputPass(Device& device, std::shared_ptr<RenderPass> pass,
        std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler,
        std::shared_ptr<Pipeline> pipeline, const uint32_t frame_slots, const bool encode_srgb,
        const bool offscreen, const float headroom)
        : m_device(device), m_render_pass(std::move(pass)), m_layout(std::move(layout)),
          m_sampler(std::move(sampler)), m_pipeline(std::move(pipeline)), m_bindings(frame_slots),
          m_encode_srgb(encode_srgb), m_offscreen(offscreen), m_headroom(headroom) {}

    Result<std::unique_ptr<OutputPass>, GraphicsError> OutputPass::create(Device& device,
        const Format output_format, const bool offscreen, const uint32_t frame_slots,
        const ImageColorSpace color_space, const float hdr_headroom) {
        using Creation = Result<std::unique_ptr<OutputPass>, GraphicsError>;
        const bool hdr = color_space == ImageColorSpace::ExtendedSrgbLinearEXT;
        if((hdr && output_format != Format::R16G16B16A16_SFLOAT)
            || (!hdr && color_space != ImageColorSpace::SrgbNonlinearKHR))
            return Creation::failure({"Unsupported output format / color space pair"});
        if(!std::isfinite(hdr_headroom) || hdr_headroom < 1.0f || hdr_headroom > 16.0f)
            return Creation::failure({"HDR headroom must be finite and between 1 and 16"});
        bool encode_srgb = false;
        switch(output_format) {
            case Format::R16G16B16A16_SFLOAT:
                if(!hdr)
                    return Creation::failure({"Floating output requires extended linear sRGB"});
                break;
            case Format::R8G8B8A8_SRGB:
            case Format::B8G8R8A8_SRGB:
                break;
            case Format::R8G8B8A8_UNORM:
            case Format::B8G8R8A8_UNORM:
                encode_srgb = true;
                break;
            default:
                return Creation::failure({"Unsupported output pass output format"});
        }
        if(frame_slots == 0)
            return Creation::failure({"Output pass requires frame slots"});
        auto color = Attachment::get_color_attachment(output_format);
        color.description.store_op = AttachmentStoreOp::Store;
        if(offscreen) {
            color.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color.usage |= ImageUsage::Sampled;
            color.usage |= ImageUsage::CopySrc;
        }
        const RenderSubPass subpass{.color_attachments = {SubpassColorAttachment(0)}};
        auto pass = RenderPass::create(device, {color}, {subpass}, output_format);
        if(!pass)
            return Creation::failure(pass.error());
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto descriptor_layout = DescriptorSetLayout::create(device, bindings);
        if(!descriptor_layout)
            return Creation::failure(descriptor_layout.error());
        auto sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Creation::failure(sampler.error());
        auto vertex = Shader::create(device, "fullscreen", DISPLAY_VERT);
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "tone_map", DISPLAY_FRAG);
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout layout;
        layout.descriptor_set_layouts = {descriptor_layout.value()};
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 12));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *pass.value());
        auto pipeline =
            pipelines.create_pipeline("tone_map", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        return Creation::success(std::unique_ptr<OutputPass>(
            new OutputPass(device, std::move(pass).value(), std::move(descriptor_layout).value(),
                std::move(sampler).value(), std::move(pipeline).value(), frame_slots, encode_srgb,
                offscreen, hdr ? hdr_headroom : 1.0f)));
    }

    Result<std::shared_ptr<OutputPass::Binding>, GraphicsError> OutputPass::create_binding(
        const std::shared_ptr<ImageView>& image) {
        using Creation = Result<std::shared_ptr<Binding>, GraphicsError>;
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::CombinedImageSampler, 1);
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return Creation::failure(pool.error());
        auto sets = pool.value()->allocate_descriptor_set(*m_layout, 1);
        if(!sets)
            return Creation::failure(sets.error());
        auto candidate = std::make_shared<Binding>(
            Binding{image, std::move(pool).value(), sets.value().front()});
        const DescriptorSet::ImageSamplerWrite write{0, *image, *m_sampler};
        candidate->descriptor.update(m_device, {}, std::span(&write, 1));
        return Creation::success(std::move(candidate));
    }

    Result<void, GraphicsError> OutputPass::render(FrameScheduler& frames,
        const std::shared_ptr<RenderTarget>& output, const std::shared_ptr<ImageView>& hdr_color,
        const float exposure) {
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_bindings.size() || !output || !hdr_color
            || !std::isfinite(exposure) || exposure < 0.0f)
            return Result<void, GraphicsError>::failure(
                {"Invalid output pass frame, input or exposure"});
        if(&hdr_color->get_image()->get_device() != &m_device
            || !(hdr_color->get_image()->get_info().usage & ImageUsage::Sampled))
            return Result<void, GraphicsError>::failure({"Invalid output pass input image"});
        const auto slot = frames.get_current_frame_slot_index();
        auto& binding = m_bindings[slot];
        if(!binding || binding->image != hdr_color) {
            auto candidate = create_binding(hdr_color);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_layout);
        frames.retain_current_frame_resource(m_sampler);
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(output);
        auto& command = frames.get_current_command_buffer();
        if(m_offscreen)
            output->begin_render_target(command, slot);
        else
            output->begin_render_target(command);
        const auto size = output->get_size();
        // 正高度 viewport 保持输入纹理与输出 framebuffer 的像素方向一致。
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_pipeline);
        command.bind_descriptor_sets(*m_pipeline->get_layout(), std::span(&binding->descriptor, 1));
        struct Parameters {
            float exposure;
            uint32_t encode_srgb;
            float headroom;
        };
        const Parameters parameters{exposure, m_encode_srgb ? 1u : 0u, m_headroom};
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &parameters, sizeof(parameters));
        command.draw(3);
        output->end_render_target(command);
        return Result<void, GraphicsError>::success();
    }
}
