#include "render/passes/skybox_pass.h"

#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/resource/texture.h"
#include "render/resource/sampled_image_binding.h"
#include "render/scene/render_submission.h"
#include "skybox_vert.h"
#include "skybox_frag.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Comet {
    Result<std::unique_ptr<SkyboxPass>, GraphicsError> SkyboxPass::create(Device& device,
        PipelineManager& pipelines, const SampleCount samples, const uint32_t frame_slots) {
        using Creation = Result<std::unique_ptr<SkyboxPass>, GraphicsError>;
        if(frame_slots == 0)
            return Creation::failure({"Skybox requires frame slots"});
        auto pass = std::unique_ptr<SkyboxPass>(new SkyboxPass(device));
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Creation::failure(layout.error());
        SamplerDesc sampler_desc{.address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge};
        const auto features =
            device.get_capability()
                .physical_device.getFormatProperties(vk::Format::eR16G16B16A16Sfloat)
                .optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
            sampler_desc.mag_filter = Filter::Nearest;
            sampler_desc.min_filter = Filter::Nearest;
            sampler_desc.mipmap_mode = SamplerMipmapMode::Nearest;
        }
        auto sampler = Sampler::create(device, sampler_desc);
        if(!sampler)
            return Creation::failure(sampler.error());
        auto vertex = Shader::create(device, "skybox", SKYBOX_VERT);
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "skybox", SKYBOX_FRAG);
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {layout.value()};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 68));
        PipelineConfig config;
        config.set_multisample_state(samples, false);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        auto pipeline = pipelines.create_pipeline(
            "skybox", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        pass->m_layout = std::move(layout).value();
        pass->m_sampler = std::move(sampler).value();
        pass->m_pipeline = std::move(pipeline).value();
        pass->m_bindings.resize(frame_slots);
        return Creation::success(std::move(pass));
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> SkyboxPass::render(
        FrameScheduler& frames, const RenderSubmission& submission) {
        using Draw = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_bindings.size())
            return Draw::failure({"Invalid skybox frame"});
        auto& binding = m_bindings[frames.get_current_frame_slot_index()];
        if(!submission.environment.background || !submission.environment_texture
            || !submission.view_project_matrix) {
            binding.reset();
            return Draw::success({});
        }
        const auto& texture = submission.environment_texture;
        const auto& image = texture->get_image_view()->get_image();
        if(&image->get_device() != &m_device || !image->get_info().cubemap
            || image->get_info().format != Format::R16G16B16A16_SFLOAT
            || !std::isfinite(submission.environment.intensity)
            || submission.environment.intensity < 0
            || !std::isfinite(submission.environment.rotation))
            return Draw::failure({"Invalid skybox environment"});
        const auto& camera = *submission.view_project_matrix;
        if(glm::determinant(camera.projection) == 0 || glm::determinant(camera.view) == 0)
            return Draw::failure({"Skybox camera matrices must be invertible"});
        const auto rotation = glm::rotate(
            Math::Mat4(1.0f), -glm::radians(submission.environment.rotation), Math::Vec3(0, 1, 0));
        const auto camera_rotation = Math::Mat4(glm::mat3(Math::inverse(camera.view)));
        const auto clip_to_environment =
            rotation * camera_rotation * Math::inverse(camera.projection);
        for(unsigned column = 0; column < 4; ++column)
            if(!Math::is_finite(clip_to_environment[column]))
                return Draw::failure({"Skybox camera transform must be finite"});
        if(!binding || binding->image != texture->get_image_view()) {
            auto candidate = SampledImageBinding::create(
                m_device, texture->get_image_view(), m_layout, m_sampler);
            if(!candidate)
                return Draw::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        auto& command = frames.get_current_command_buffer();
        command.bind_pipeline(*m_pipeline);
        command.bind_descriptor_sets(*m_pipeline->get_layout(), std::span(&binding->descriptor, 1));
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &clip_to_environment, sizeof(clip_to_environment));
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            64, &submission.environment.intensity, sizeof(float));
        command.draw(3);
        const auto completion = texture->get_ready_completion();
        if(completion.is_valid())
            return Draw::success({QueueSemaphoreSubmit(
                completion, Flags<PipelineStage>(PipelineStage::FragmentShader))});
        return Draw::success({});
    }
}
