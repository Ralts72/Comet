#include "render/passes/volumetric_fog_pass.h"

#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "render/frame_scheduler.h"
#include "render/scene/render_submission.h"
#include "display_vert.h"
#include "fog_background_frag.h"
#include "volumetric_fog_comp.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        struct Parameters {
            Math::Vec4 range, medium, tint_ambient;
        };
        static_assert(sizeof(Parameters) == 48);
    }
    struct VolumetricFogPass::Program {
        std::shared_ptr<DescriptorSetLayout> view_layout, descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
    };
    struct VolumetricFogPass::Resources {
        struct Frame {
            std::shared_ptr<ImageView> atlas;
            std::shared_ptr<CPUBuffer> parameters;
            std::shared_ptr<Program> program;
            std::unique_ptr<DescriptorPool> pool;
            std::optional<DescriptorSet> descriptor;
        };
        std::vector<std::shared_ptr<Frame>> frames;
    };

    VolumetricFogPass::VolumetricFogPass(Device& device) : m_device(device) {}
    VolumetricFogPass::~VolumetricFogPass() = default;

    Math::Vec2u VolumetricFogPass::atlas_size(Math::Vec2u size) {
        return Math::Vec2u(
                   std::clamp((size.x + 7) / 8, 1u, 192u), std::clamp((size.y + 7) / 8, 1u, 128u))
               * 8u;
    }

    Result<std::unique_ptr<VolumetricFogPass>, GraphicsError> VolumetricFogPass::create(
        Device& device, PipelineManager& pipelines,
        std::shared_ptr<DescriptorSetLayout> view_layout, SampleCount samples) {
        using Created = Result<std::unique_ptr<VolumetricFogPass>, GraphicsError>;
        const auto& capability = device.get_capability();
        const auto features =
            capability.physical_device.getFormatProperties(vk::Format::eR16G16B16A16Sfloat)
                .optimalTilingFeatures;
        const auto queues = capability.physical_device.getQueueFamilyProperties();
        if(!(features & vk::FormatFeatureFlagBits::eStorageImage)
            || !(features & vk::FormatFeatureFlagBits::eSampledImage)
            || !(queues.at(*capability.graphics_queue_family.queue_family_index).queueFlags
                 & vk::QueueFlagBits::eCompute))
            return Created::failure({"体积雾需要 compute 与 RGBA16F storage image"});
        auto result = std::unique_ptr<VolumetricFogPass>(new VolumetricFogPass(device));
        auto program = std::make_shared<Program>();
        program->view_layout = std::move(view_layout);
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::StorageImage, Flags<ShaderStage>(ShaderStage::Compute));
        bindings.add_binding(
            1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(device, bindings);
        if(!descriptors)
            return Created::failure(descriptors.error());
        program->descriptors = std::move(descriptors).value();
        const std::array sets{program->view_layout->get(), program->descriptors->get()};
        const vk::PipelineLayoutCreateInfo layout_info({}, sets);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            device.get(), "Create fog layout", [&](auto* output) noexcept {
                return device.get().createPipelineLayout(&layout_info, nullptr, output);
            });
        if(!layout)
            return Created::failure(layout.error());
        program->layout = std::move(layout).value();
        auto compute = Shader::create(device, "volumetric fog", VOLUMETRIC_FOG_COMP);
        if(!compute)
            return Created::failure(compute.error());
        const vk::PipelineShaderStageCreateInfo stage(
            {}, vk::ShaderStageFlagBits::eCompute, compute.value()->get(), "main");
        const vk::ComputePipelineCreateInfo info({}, stage, program->layout.get());
        auto pipeline = Graphics::create_handle<vk::Pipeline>(
            device.get(), "Create fog pipeline", [&](auto* output) noexcept {
                return device.get().createComputePipelines({}, 1, &info, nullptr, output);
            });
        if(!pipeline)
            return Created::failure(pipeline.error());
        program->pipeline = std::move(pipeline).value();
        result->m_program = std::move(program);
        auto vertex = Shader::create(device, "fullscreen", DISPLAY_VERT);
        if(!vertex)
            return Created::failure(vertex.error());
        auto fragment = Shader::create(device, "fog background", FOG_BACKGROUND_FRAG);
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {result->m_program->view_layout};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.set_multisample_state(samples, false);
        config.set_color_blend_attachment_state({.blend_enable = true,
            .src_color_blend_factor = BlendFactor::One,
            .dst_color_blend_factor = BlendFactor::SrcAlpha,
            .src_alpha_blend_factor = BlendFactor::Zero,
            .dst_alpha_blend_factor = BlendFactor::One});
        auto background = pipelines.create_pipeline(
            "fog background", shader_layout, config, vertex.value(), fragment.value());
        if(!background)
            return Created::failure(background.error());
        result->m_background = std::move(background).value();
        return Created::success(std::move(result));
    }

    Result<std::shared_ptr<VolumetricFogPass::Resources>, GraphicsError> VolumetricFogPass::
        create_resources(
            const RenderGraph::Resources& images, RenderGraph::ResourceId atlas, uint32_t slots) {
        using Created = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto resources = std::make_shared<Resources>();
        for(uint32_t slot = 0; slot < slots; ++slot) {
            auto frame = std::make_shared<Resources::Frame>();
            frame->program = m_program;
            const auto& image =
                std::get<std::shared_ptr<Image>>(images.get_bindings(slot)[atlas.index]);
            auto view =
                ImageView::try_create(m_device, image, Flags<ImageAspect>(ImageAspect::Color));
            if(!view)
                return Created::failure(view.error());
            frame->atlas = std::move(view).value();
            auto buffer =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(Parameters), false, nullptr, "fog parameters");
            if(!buffer)
                return Created::failure(buffer.error());
            frame->parameters = std::move(buffer).value();
            DescriptorPoolSizes sizes;
            sizes.add_pool_size(DescriptorType::UniformBuffer, 1);
            sizes.add_pool_size(DescriptorType::StorageImage, 1);
            auto pool = DescriptorPool::create(m_device, 1, sizes);
            if(!pool)
                return Created::failure(pool.error());
            frame->pool = std::move(pool).value();
            auto sets = frame->pool->allocate_descriptor_set(*m_program->descriptors, 1);
            if(!sets)
                return Created::failure(sets.error());
            frame->descriptor = sets.value().front();
            const DescriptorSet::UniformBufferWrite uniform{
                1, *frame->parameters, sizeof(Parameters)};
            frame->descriptor->update(m_device, std::span(&uniform, 1));
            const vk::DescriptorImageInfo storage(
                {}, frame->atlas->get(), vk::ImageLayout::eGeneral);
            const vk::WriteDescriptorSet write(
                frame->descriptor->get(), 0, 0, 1, vk::DescriptorType::eStorageImage, &storage);
            m_device.get().updateDescriptorSets(write, {});
            resources->frames.push_back(std::move(frame));
        }
        return Created::success(std::move(resources));
    }

    FogVolume VolumetricFogPass::prepare_frame(const Resources& resources, uint32_t slot,
        const RenderSubmission& submission, Math::Vec2u size) const {
        const auto& settings = submission.world.environment.volumetric_fog;
        const auto& camera = *submission.view_project_matrix;
        // Engine projection convention: depth 0 is near for both perspective and orthographic.
        const auto inverse = Math::inverse(camera.projection);
        const auto near_h = inverse * Math::Vec4(0, 0, 0, 1);
        const float near = std::max(.001f, std::abs(near_h.z / near_h.w));
        const float far = std::max(near * 1.01f, settings.max_distance);
        const float log_range = std::log(far / near);
        const Parameters parameters{{near, far, log_range, 0},
            {settings.density, settings.base_height, settings.height_falloff, settings.anisotropy},
            {settings.color, settings.ambient}};
        const auto& frame = resources.frames.at(slot);
        frame->parameters->write(&parameters);
        return {frame->atlas, {near, 1 / log_range, 1.0f / size.x, 1.0f / size.y}};
    }

    void VolumetricFogPass::render(
        FrameScheduler& frames, const Resources& resources, const DescriptorSet& view) {
        const auto& frame = resources.frames.at(frames.get_current_frame_slot_index());
        frames.retain_current_frame_resource(frame);
        const auto command = frames.get_current_command_buffer().get();
        command.bindPipeline(vk::PipelineBindPoint::eCompute, m_program->pipeline.get());
        const std::array sets{view.get(), frame->descriptor->get()};
        command.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, m_program->layout.get(), 0, sets, {});
        const auto size = frame->atlas->get_image()->get_info().extent;
        command.dispatch((size.x / 8 + 7) / 8, (size.y / 8 + 7) / 8, 1);
    }

    void VolumetricFogPass::render_background(
        FrameScheduler& frames, const DescriptorSet& view, Math::Vec2u size) {
        frames.retain_current_frame_resource(m_background);
        auto& command = frames.get_current_command_buffer();
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.bind_pipeline(*m_background);
        command.bind_descriptor_sets(*m_background->get_layout(), std::span(&view, 1));
        command.draw(3);
        command.set_viewport(Graphics::get_viewport(float(size.x), float(size.y)));
    }
}
