#include "render/passes/temporal_pass.h"

#include "graphics/attachment.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "render/resource/sampled_image_binding.h"
#include "display_vert.h"
#include "temporal_frag.h"

namespace Comet {
    namespace {
        float halton(uint32_t index, uint32_t base) {
            float value = 0, fraction = 1;
            for(; index; index /= base) {
                fraction /= float(base);
                value += float(index % base) * fraction;
            }
            return value;
        }
        Math::Vec4 projection_coefficients(const Math::Mat4& projection) {
            return {projection[2][2], projection[3][2], projection[2][3], projection[3][3]};
        }
    }

    TemporalPass::TemporalPass(Device& device) : m_device(device) {}
    TemporalPass::~TemporalPass() = default;

    Result<std::unique_ptr<TemporalPass>, GraphicsError> TemporalPass::create(
        Device& device, uint32_t frame_slots) {
        using Created = Result<std::unique_ptr<TemporalPass>, GraphicsError>;
        if(frame_slots == 0)
            return Created::failure({"Temporal pass requires frame slots"});
        auto result = std::unique_ptr<TemporalPass>(new TemporalPass(device));
        auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        color.usage |= ImageUsage::Sampled;
        color.usage |= ImageUsage::CopySrc;
        auto pass = RenderPass::create(
            device, {color}, {RenderSubPass{.color_attachments = {SubpassColorAttachment(0)}}});
        if(!pass)
            return Created::failure(pass.error());
        result->m_pass = std::move(pass).value();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t index = 0; index < 4; ++index)
            bindings.add_binding(index, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Created::failure(layout.error());
        result->m_layout = std::move(layout).value();
        auto sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Created::failure(sampler.error());
        result->m_sampler = std::move(sampler).value();
        auto vertex = Shader::create(device, "fullscreen", DISPLAY_VERT);
        if(!vertex)
            return Created::failure(vertex.error());
        auto fragment = Shader::create(device, "temporal resolve", TEMPORAL_FRAG);
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {result->m_layout};
        shader_layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 28)};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *result->m_pass);
        auto pipeline = pipelines.create_pipeline(
            "temporal resolve", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        result->m_pipeline = std::move(pipeline).value();
        result->m_bindings.resize(frame_slots);
        return Created::success(std::move(result));
    }

    Result<TemporalPass::Camera, GraphicsError> TemporalPass::prepare(FrameScheduler& frames,
        uint64_t scene_lifetime, const ViewProjectMatrix& camera, Math::Vec2u size) {
        using Prepared = Result<Camera, GraphicsError>;
        const auto serial = frames.get_current_frame_serial();
        if(!m_target || m_target->get_size() != size || m_initialized_serial == serial
            || m_recorded_serial == serial) {
            auto target = RenderTarget::try_create_multi_target(m_device, *m_pass, size, 2);
            if(!target)
                return Prepared::failure(target.error());
            m_target = std::move(target).value();
            reset();
            auto command = frames.get_current_command_buffer().get();
            for(uint32_t index = 0; index < 2; ++index) {
                const vk::ImageMemoryBarrier barrier({}, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                    m_target->get_color_view(index)->get_image()->get(),
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
                command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);
            }
            m_initialized_serial = serial;
        }
        const auto previous_world = Math::inverse(m_unjittered.view);
        const auto current_world = Math::inverse(camera.view);
        const bool cut =
            glm::distance(Math::Vec3(previous_world[3]), Math::Vec3(current_world[3])) > 5
            || glm::dot(Math::Vec3(previous_world[2]), Math::Vec3(current_world[2])) < .75f;
        m_history_valid = m_recorded_serial && m_recorded_serial + 1 == serial
                          && m_lifetime == scene_lifetime
                          && m_unjittered.projection == camera.projection && !cut;
        Camera prepared{camera};
        if(m_history_valid) {
            prepared.previous = m_camera.projection * m_camera.view;
            prepared.previous_projection = projection_coefficients(m_camera.projection);
        } else
            m_sample = 0;
        const uint32_t sample = (m_sample++ % 8) + 1;
        const Math::Vec2 jitter{(halton(sample, 2) - .5f) * 2 / float(size.x),
            (halton(sample, 3) - .5f) * 2 / float(size.y)};
        const auto color_jitter = jitter * Math::Vec2(.5f, -.5f);
        m_jitter_delta = m_history_valid ? color_jitter - m_jitter : Math::Vec2(0);
        m_jitter = color_jitter;
        for(int column = 0; column < 4; ++column) {
            prepared.current.projection[column][0] += jitter.x * camera.projection[column][3];
            prepared.current.projection[column][1] += jitter.y * camera.projection[column][3];
        }
        m_camera = prepared.current;
        m_unjittered = camera;
        m_lifetime = scene_lifetime;
        m_index ^= 1;
        return Prepared::success(prepared);
    }

    Result<void, GraphicsError> TemporalPass::render(FrameScheduler& frames,
        const std::shared_ptr<ImageView>& color, const std::shared_ptr<ImageView>& depth,
        const std::shared_ptr<ImageView>& motion) {
        const auto slot = frames.get_current_frame_slot_index();
        const std::vector images{color, depth, motion, history()};
        auto& binding = m_bindings.at(slot);
        if(!binding || binding->images != images) {
            auto created = SampledImageBinding::create(m_device, images, m_layout, m_sampler);
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            binding = std::move(created).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_target);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_pass);
        auto& command = frames.get_current_command_buffer();
        m_target->begin_render_target(command, m_index);
        const auto size = m_target->get_size();
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_pipeline);
        command.bind_descriptor_sets(*m_pipeline->get_layout(), std::span(&binding->descriptor, 1));
        struct Parameters {
            Math::Vec4 projection;
            Math::Vec2 jitter_delta;
            uint32_t valid;
        };
        const Parameters parameters{projection_coefficients(m_camera.projection), m_jitter_delta,
            m_history_valid ? 1u : 0u};
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &parameters, 28);
        command.draw(3);
        m_target->end_render_target(command);
        m_recorded_serial = frames.get_current_frame_serial();
        return Result<void, GraphicsError>::success();
    }
    std::shared_ptr<ImageView> TemporalPass::output() const {
        return m_target ? m_target->get_color_view(m_index) : nullptr;
    }
    std::shared_ptr<ImageView> TemporalPass::history() const {
        return m_target->get_color_view(m_index ^ 1);
    }
    void TemporalPass::reset() {
        m_recorded_serial = 0;
        m_sample = 0;
        m_history_valid = false;
    }
}
