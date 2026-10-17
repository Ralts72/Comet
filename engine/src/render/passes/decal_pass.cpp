#include "render/passes/decal_pass.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/scene/draw_order.h"
#include "render/resource/sampled_image_binding.h"
#include "render/resource/texture.h"
#include "display_vert.h"
#include "decal_frag.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Comet {
    namespace {
        vk::Rect2D decal_scissor(
            const BoundingBox& bounds, const Math::Mat4& matrix, Math::Vec2u size) {
            Math::Vec2 lower{float(size.x), float(size.y)}, upper{0};
            for(unsigned corner = 0; corner < 8; ++corner) {
                const Math::Vec3 point{corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                    corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                    corner & 4 ? bounds.maximum.z : bounds.minimum.z};
                const auto clip = matrix * Math::Vec4(point, 1);
                if(clip.w <= 0)
                    return {{0, 0}, {size.x, size.y}};
                const auto pixel =
                    (Math::Vec2(clip) / clip.w * Math::Vec2(.5f, -.5f) + .5f) * Math::Vec2(size);
                lower = Math::min(lower, pixel);
                upper = Math::max(upper, pixel);
            }
            lower = Math::clamp(Math::floor(lower), Math::Vec2(0), Math::Vec2(size));
            upper = Math::clamp(Math::ceil(upper), lower, Math::Vec2(size));
            return {{int32_t(lower.x), int32_t(lower.y)},
                {uint32_t(upper.x - lower.x), uint32_t(upper.y - lower.y)}};
        }
    }

    Result<std::unique_ptr<DecalPass>, GraphicsError> DecalPass::create(Device& device,
        PipelineManager& pipelines, std::shared_ptr<Texture> white,
        std::shared_ptr<DescriptorSetLayout> view_layout, uint32_t frame_slots,
        SampleCount samples) {
        using Created = Result<std::unique_ptr<DecalPass>, GraphicsError>;
        auto result = std::unique_ptr<DecalPass>(new DecalPass(device));
        DescriptorSetLayoutBindings bindings;
        for(uint32_t binding = 0; binding < 2; ++binding)
            bindings.add_binding(binding, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Created::failure(layout.error());
        result->m_layout = std::move(layout).value();
        auto sampler =
            Sampler::create(device, {.address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Created::failure(sampler.error());
        result->m_color_sampler = std::move(sampler).value();
        auto depth_sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!depth_sampler)
            return Created::failure(depth_sampler.error());
        result->m_depth_sampler = depth_sampler.value();
        result->m_white = std::move(white);
        auto vertex = Shader::create(device, "fullscreen", DISPLAY_VERT);
        if(!vertex)
            return Created::failure(vertex.error());
        auto fragment = Shader::create(device, "projected decal", DECAL_FRAG);
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {std::move(view_layout), result->m_layout};
        shader_layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 96)};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.set_multisample_state(samples, false);
        config.enable_alpha_blend();
        config.color_blend_state.alphaBlendOp = vk::BlendOp::eMax;
        auto pipeline = pipelines.create_pipeline(
            "projected decal", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        result->m_pipeline = std::move(pipeline).value();
        result->m_bindings.resize(frame_slots);
        return Created::success(std::move(result));
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> DecalPass::render(
        FrameScheduler& frames, const RenderSubmission& submission, const DescriptorSet& view,
        const std::shared_ptr<ImageView>& depth, Math::Vec2u size) {
        using Drawn = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!submission.view_project_matrix || !depth)
            return Drawn::success({});
        const auto& camera = *submission.view_project_matrix;
        const auto matrix = camera.projection * camera.view;
        const auto frustum = Frustum::from_view_projection(matrix);
        m_visible.clear();
        for(const auto& resolved : submission.world.decals) {
            const auto& decal = resolved.decal;
            if((decal.texture && !resolved.texture) || !decal.bounds.is_valid()
                || (frustum && !frustum->intersects(decal.bounds)))
                continue;
            m_visible.push_back(&resolved);
        }
        DrawOrder::sort_if_needed(m_visible, [](const auto* a, const auto* b) {
            return std::pair(a->decal.draw_order, a->decal.entity_id)
                   < std::pair(b->decal.draw_order, b->decal.entity_id);
        });
        const auto excess = uint32_t(m_visible.size() > 128 ? m_visible.size() - 128 : 0);
        if(excess && excess != m_excess)
            LOG_WARN("Decals omitted {} excess visible projectors (limit 128)", excess);
        m_excess = excess;
        if(m_visible.size() > 128)
            m_visible.resize(128);
        auto& cache = m_bindings.at(frames.get_current_frame_slot_index());
        for(auto& [texture, binding] : cache)
            binding.used = false;
        const ScopeExit release([&] {
            m_visible.clear();
            std::erase_if(cache, [](const auto& entry) { return !entry.second.used; });
        });
        auto& command = frames.get_current_command_buffer();
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        const ScopeExit restore([&] {
            command.set_viewport(Graphics::get_viewport(float(size.x), float(size.y)));
            command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        });
        std::vector<QueueSemaphoreSubmit> waits;
        for(const auto* resolved : m_visible) {
            const auto& decal = resolved->decal;
            const auto scissor = decal_scissor(decal.bounds, matrix, size);
            if(!scissor.extent.width || !scissor.extent.height)
                continue;
            const auto texture = resolved->texture ? resolved->texture : m_white;
            auto& cached = cache[texture.get()];
            cached.used = true;
            const auto color = texture->get_image_view();
            if(color->get_image()->get_info().cubemap)
                return Drawn::failure({"Decal texture must be two-dimensional"});
            const auto sampler = texture->get_sampler() ? texture->get_sampler() : m_color_sampler;
            if(!cached.images || cached.images->images[0] != color
                || cached.images->images[1] != depth || cached.images->samplers[0] != sampler) {
                auto binding = SampledImageBinding::create(
                    m_device, {color, depth}, m_layout, std::vector{sampler, m_depth_sampler});
                if(!binding)
                    return Drawn::failure(binding.error());
                cached.images = std::move(binding).value();
            }
            frames.retain_current_frame_resource(cached.images);
            frames.retain_current_frame_resource(texture);
            frames.retain_current_frame_resource(m_pipeline);
            command.set_scissor(scissor);
            command.bind_pipeline(*m_pipeline);
            const std::array descriptors{view, cached.images->descriptor};
            command.bind_descriptor_sets(*m_pipeline->get_layout(), descriptors);
            struct Parameters {
                Math::Mat4 world_to_local;
                Math::Vec4 color, parameters;
            };
            const Parameters parameters{decal.world_to_local, decal.color,
                {decal.roughness, decal.normal_threshold, decal.edge_fade, 0}};
            command.push_constants(*m_pipeline->get_layout(),
                Flags<ShaderStage>(ShaderStage::Fragment), 0, &parameters, sizeof(parameters));
            command.draw(3);
            const auto& ready = texture->get_ready_completion();
            if(ready.is_valid() && !ready.is_complete())
                merge_semaphore_wait(
                    waits, QueueSemaphoreSubmit(
                               ready, Flags<PipelineStage>(PipelineStage::FragmentShader)));
        }
        return Drawn::success(std::move(waits));
    }
}
