#include "render/material_renderer.h"

#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_manager.h"
#include "render/resource/texture.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"
#include "material_lit_vert.h"
#include "material_lit_frag.h"
#include "material_pbr_frag.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_set>

namespace Comet {
    namespace {
        struct BuiltinPipeline {
            const char* layout;
            const char* vertex;
            const char* fragment;
            std::span<const uint32_t> vertex_code;
            std::span<const uint32_t> fragment_code;
        };
        constexpr BuiltinPipeline BUILTIN_PIPELINES[]{
            {"cube_texture", "material_mesh", "material_textured", MATERIAL_MESH_VERT,
                MATERIAL_TEXTURED_FRAG},
            {"unlit_color", "material_mesh", "material_solid", MATERIAL_MESH_VERT,
                MATERIAL_SOLID_FRAG},
            {"lit_color", "material_lit_vert", "material_lit_frag", MATERIAL_LIT_VERT,
                MATERIAL_LIT_FRAG},
            {"pbr_color", "material_lit_vert", "material_pbr_frag", MATERIAL_LIT_VERT,
                MATERIAL_PBR_FRAG}};

        void append_wait(std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
            if(!completion.is_valid())
                return;
            merge_semaphore_wait(waits, QueueSemaphoreSubmit(completion, stages));
        }
    }

    MaterialRenderer::MaterialRenderer(Device& device, PipelineManager& pipelines,
        ResourceManager& resources, const uint32_t frame_slot_count,
        const SampleCount samples)
        : m_device(device) {
        static_assert(sizeof(FrameData) == 160);
        static_assert(offsetof(FrameData, camera_position) == 128);
        static_assert(offsetof(FrameData, view_direction) == 144);
        m_sampler = resources.get_sampler_manager().get_linear_repeat();
        // 独立使用 MaterialRenderer 时也必须提供有效的 sampler descriptor。
        const auto fallback =
            resources
                .try_create_texture(
                    {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}})
                .value();
        if(fallback->get_ready_completion().is_valid())
            fallback->get_ready_completion().wait();
        m_fallback_shadow = fallback->get_image_view();
        DescriptorSetLayoutBindings frame_bindings;
        frame_bindings.add_binding(0, DescriptorType::UniformBuffer,
            Flags<ShaderStage>(ShaderStage::Vertex) | ShaderStage::Fragment);
        frame_bindings.add_binding(
            1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        frame_bindings.add_binding(2, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        m_frame_layout = std::make_shared<DescriptorSetLayout>(device, frame_bindings);
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slot_count * 2);
        pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler, frame_slot_count);
        const auto pool =
            std::make_shared<DescriptorPool>(device, frame_slot_count, pool_sizes);
        const auto descriptors =
            pool->allocate_descriptor_set(*m_frame_layout, frame_slot_count);
        for(uint32_t slot = 0; slot < frame_slot_count; ++slot) {
            auto frame = std::make_shared<FrameResources>();
            frame->shadow_sampler = resources.get_sampler_manager().get_nearest_clamp();
            frame->layout = m_frame_layout;
            frame->pool = pool;
            frame->buffer = Buffer::try_create_cpu_buffer(device,
                Flags<BufferUsage>(BufferUsage::Uniform), sizeof(FrameData), false,
                nullptr, "frame view-project")
                                .value();
            frame->descriptor = descriptors[slot];
            const vk::DescriptorBufferInfo info(
                frame->buffer->get(), 0, sizeof(FrameData));
            vk::WriteDescriptorSet write;
            write.dstSet = frame->descriptor->get();
            write.dstBinding = 0;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &info;
            device.get().updateDescriptorSets(write, {});
            frame->lighting = Buffer::try_create_cpu_buffer(device,
                Flags<BufferUsage>(BufferUsage::Uniform), sizeof(LightingData), false,
                nullptr, "frame lighting")
                                  .value();
            const vk::DescriptorBufferInfo lighting_info(
                frame->lighting->get(), 0, sizeof(LightingData));
            write.dstBinding = 1;
            write.pBufferInfo = &lighting_info;
            device.get().updateDescriptorSets(write, {});
            m_frames.push_back(std::move(frame));
        }
        auto& shaders = resources.get_shader_manager();
        for(const auto& builtin : BUILTIN_PIPELINES)
            m_pipelines.emplace(builtin.layout,
                create_pipeline(pipelines,
                    shaders.load_shader_if_missing(builtin.vertex, builtin.vertex_code),
                    shaders.load_shader_if_missing(
                        builtin.fragment, builtin.fragment_code),
                    MaterialLayout::find_builtin(builtin.layout), samples));
    }

    std::shared_ptr<const MaterialRenderer::PipelineState> MaterialRenderer::
        create_pipeline(PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment,
            std::shared_ptr<const MaterialLayout> layout, const SampleCount samples,
            std::shared_ptr<DescriptorSetLayout> material_layout) {
        layout = MaterialLayout::reflect(layout, fragment->get_interface());
        auto state = std::make_shared<PipelineState>();
        state->layout = std::move(layout);
        state->frame_layout = m_frame_layout;
        DescriptorSetLayoutBindings bindings;
        if(state->layout->get_parameter_size() > 0) {
            bindings.add_binding(state->layout->get_parameter_binding(),
                DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        }
        for(const auto& texture : state->layout->get_textures()) {
            bindings.add_binding(texture.binding, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        }
        state->material_layout = std::move(material_layout);
        if(!state->material_layout)
            state->material_layout =
                std::make_shared<DescriptorSetLayout>(m_device, bindings);
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {m_frame_layout, state->material_layout};
        shader_layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex, 0, sizeof(PushConstant)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(
            0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        input.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        input.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_input_assembly_state(Topology::TriangleList);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        config.set_multisample_state(samples, false, 0.2f);
        state->pipeline = pipelines.create_pipeline(
            state->layout->get_name(), shader_layout, config, vertex, fragment);
        return state;
    }

    MaterialRenderer::ReloadReport MaterialRenderer::reload_shaders(
        PipelineManager& pipelines, ShaderManager& shaders,
        const ShaderManager::Bytecodes& bytecodes, const SampleCount samples) {
        std::unordered_set<std::string_view> known;
        for(const auto& builtin : BUILTIN_PIPELINES) {
            known.insert(builtin.vertex);
            known.insert(builtin.fragment);
            if(bytecodes.contains(builtin.vertex) != bytecodes.contains(builtin.fragment))
                throw std::invalid_argument(
                    "Material Shader reload requires all consumers of a shared vertex");
        }
        if(bytecodes.empty() || std::ranges::any_of(bytecodes, [&](const auto& entry) {
               return !known.contains(entry.first);
           }))
            throw std::invalid_argument(
                "Material Shader reload requires known non-empty cohorts");
        auto candidate_shaders = shaders.prepare_update(bytecodes);
        for(const auto& [name, candidate] : candidate_shaders) {
            if(!bytecodes.contains(name))
                continue;
            std::optional<uint32_t> ignored_set;
            if(candidate->get_interface().get_stage()
                == vk::ShaderStageFlagBits::eFragment)
                ignored_set = 1;
            if(!shaders.get_shader(name)->get_interface().has_same_layout(
                   candidate->get_interface(), ignored_set))
                throw std::invalid_argument(
                    "Shader changed a fixed renderer interface: " + name);
        }
        ReloadReport report;
        auto candidate_pipelines = m_pipelines;
        for(const auto& builtin : BUILTIN_PIPELINES) {
            const auto& [layout, vertex, fragment, vertex_code, fragment_code] = builtin;
            if(!bytecodes.contains(fragment))
                continue;
            const auto& old = m_pipelines.at(layout);
            const auto reflected = MaterialLayout::reflect(
                old->layout, candidate_shaders.at(fragment)->get_interface());
            std::shared_ptr<DescriptorSetLayout> descriptor_layout;
            if(reflected == old->layout)
                descriptor_layout = old->material_layout;
            auto candidate = create_pipeline(pipelines, candidate_shaders.at(vertex),
                candidate_shaders.at(fragment), reflected, samples,
                std::move(descriptor_layout));
            if(candidate->pipeline != old->pipeline) {
                candidate_pipelines.at(layout) = std::move(candidate);
                ++report.pipelines;
            }
        }
        auto candidate_prepared = m_prepared;
        auto candidate_materials = m_materials;
        for(auto& [handle, cached] : candidate_materials) {
            if(!cached.resources)
                continue;
            const auto& pipeline =
                candidate_pipelines.at(cached.resources->prepared->layout->get_name());
            if(pipeline == cached.resources->pipeline)
                continue;
            const auto prepared = candidate_prepared.rebind(handle, pipeline->layout);
            if(!prepared)
                throw std::runtime_error(
                    "Cannot rebuild resident material " + std::to_string(handle.value()));
            auto candidate = create_material(prepared, pipeline, cached.resources);
            if(candidate->pool != cached.resources->pool)
                ++report.material_bindings;
            cached.resources = std::move(candidate);
            cached.failed_candidate.reset();
            cached.failed_pipeline.reset();
            cached.retry_after_serial = 0;
            ++report.material_versions;
        }
        shaders.publish_update(candidate_shaders);
        m_pipelines.swap(candidate_pipelines);
        m_prepared.swap(candidate_prepared);
        m_materials.swap(candidate_materials);
        return report;
    }

    std::vector<std::shared_ptr<const MaterialLayout>> MaterialRenderer::
        get_material_layouts() const {
        std::vector<std::shared_ptr<const MaterialLayout>> layouts;
        for(const auto& [name, pipeline] : m_pipelines)
            layouts.push_back(pipeline->layout);
        return layouts;
    }

    std::shared_ptr<MaterialRenderer::MaterialResources> MaterialRenderer::
        prepare_material(const MaterialBinding& material, const uint64_t frame_serial) {
        if(!material.resource)
            return nullptr;
        const auto pipeline = m_pipelines.find(material.resource->get_template_name());
        if(pipeline == m_pipelines.end()) {
            const auto [entry, inserted] =
                m_unsupported.try_emplace(material.material_handle, frame_serial);
            entry->second = frame_serial;
            if(inserted) {
                LOG_ERROR("Unsupported material layout '{}' for handle {}",
                    material.resource->get_template_name(),
                    material.material_handle.value());
            }
            return nullptr;
        }
        m_unsupported.erase(material.material_handle);
        const auto prepared = m_prepared.prepare(
            material.material_handle, material.resource, pipeline->second->layout);
        if(!prepared)
            return nullptr;
        auto& cached = m_materials[material.material_handle];
        cached.used = true;
        if(cached.resources && cached.resources->prepared == prepared
            && cached.resources->pipeline == pipeline->second)
            return cached.resources;
        if(cached.failed_candidate == prepared
            && cached.failed_pipeline == pipeline->second
            && frame_serial < cached.retry_after_serial) {
            return cached.resources;
        }
        try {
            auto candidate =
                create_material(prepared, pipeline->second, cached.resources);
            if(!cached.resources || candidate->pool != cached.resources->pool)
                ++m_statistics.material_bindings_created;
            cached.resources = candidate;
            cached.failed_candidate.reset();
            cached.failed_pipeline.reset();
            ++m_statistics.material_versions_created;
            return candidate;
        } catch(const std::exception& exception) {
            LOG_ERROR("Keeping previous GPU material for handle {}: {}",
                material.material_handle.value(), exception.what());
            cached.failed_candidate = prepared;
            cached.failed_pipeline = pipeline->second;
            cached.retry_after_serial = frame_serial + 60;
            return cached.resources;
        }
    }

    std::shared_ptr<MaterialRenderer::MaterialResources> MaterialRenderer::
        create_material(const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous) {
        if(previous && previous->prepared == prepared
            && previous->pipeline->material_layout == pipeline->material_layout) {
            auto candidate = std::make_shared<MaterialResources>(*previous);
            candidate->pipeline = pipeline;
            return candidate;
        }
        auto candidate = std::make_shared<MaterialResources>();
        candidate->pipeline = pipeline;
        candidate->prepared = prepared;
        candidate->sampler = m_sampler;
        DescriptorPoolSizes sizes;
        if(!prepared->parameters.empty()) {
            auto buffer = Buffer::try_create_cpu_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::Uniform), prepared->parameters.size(),
                true, prepared->parameters.data(), "immutable material parameters");
            if(!buffer) {
                throw std::runtime_error("Material parameter allocation failed: "
                                         + vk::to_string(buffer.result()));
            }
            candidate->parameters = std::move(buffer).value();
            sizes.add_pool_size(DescriptorType::UniformBuffer, 1);
        }
        if(!prepared->textures.empty()) {
            sizes.add_pool_size(DescriptorType::CombinedImageSampler,
                static_cast<uint32_t>(prepared->textures.size()));
        }
        candidate->pool = std::make_shared<DescriptorPool>(m_device, 1, sizes);
        candidate->descriptor =
            candidate->pool
                ->allocate_descriptor_set(*candidate->pipeline->material_layout, 1)
                .front();
        std::vector<vk::WriteDescriptorSet> writes;
        vk::DescriptorBufferInfo buffer_info;
        if(candidate->parameters) {
            buffer_info = vk::DescriptorBufferInfo(
                candidate->parameters->get(), 0, candidate->parameters->get_size());
            vk::WriteDescriptorSet write;
            write.dstSet = candidate->descriptor->get();
            write.dstBinding = prepared->layout->get_parameter_binding();
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.pBufferInfo = &buffer_info;
            writes.push_back(write);
        }
        std::vector<vk::DescriptorImageInfo> images(prepared->textures.size());
        for(std::size_t index = 0; index < images.size(); ++index) {
            const auto& binding = prepared->textures[index];
            images[index] = vk::DescriptorImageInfo(m_sampler->get(),
                binding.texture->get_image_view()->get(),
                vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet write;
            write.dstSet = candidate->descriptor->get();
            write.dstBinding = binding.binding;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.pImageInfo = &images[index];
            writes.push_back(write);
        }
        m_device.get().updateDescriptorSets(writes, {});
        return candidate;
    }

    std::vector<QueueSemaphoreSubmit> MaterialRenderer::render(FrameScheduler& frames,
        const ViewProjectMatrix& view, const std::span<const ResolvedRenderItem> items,
        const LightingData& lighting, std::shared_ptr<ImageView> shadow_map) {
        if(!frames.is_recording_frame())
            throw std::logic_error(
                "Material rendering requires an active recording frame");
        const auto previous_omissions =
            std::pair(m_statistics.excess_lights, m_statistics.invalid_lights);
        m_statistics = {};
        m_statistics.frame_set_count = static_cast<uint32_t>(m_frames.size());
        const auto& frame = m_frames.at(frames.get_current_frame_slot_index());
        const auto camera_world = Math::inverse(view.view);
        const FrameData frame_data{view, camera_world[3],
            Math::Vec4(
                Math::Vec3(camera_world[2]), view.projection[2][3] == 0 ? 1.0f : 0.0f)};
        frame->buffer->write(&frame_data);
        frame->lighting->write(&lighting);
        if(!shadow_map)
            shadow_map = m_fallback_shadow;
        if(frame->shadow_map != shadow_map) {
            const vk::DescriptorImageInfo image(frame->shadow_sampler->get(),
                shadow_map->get(), vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet write;
            write.dstSet = frame->descriptor->get();
            write.dstBinding = 2;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.pImageInfo = &image;
            m_device.get().updateDescriptorSets(write, {});
            frame->shadow_map = std::move(shadow_map);
        }
        m_statistics.light_count = static_cast<uint32_t>(lighting.counts.x);
        m_statistics.excess_lights = static_cast<uint32_t>(lighting.counts.y);
        m_statistics.invalid_lights = static_cast<uint32_t>(lighting.counts.z);
        if((m_statistics.excess_lights > 0 || m_statistics.invalid_lights > 0)
            && previous_omissions
                   != std::pair(m_statistics.excess_lights, m_statistics.invalid_lights))
            LOG_WARN(
                "Lighting omitted {} lights beyond the {}-light limit and {} invalid lights",
                m_statistics.excess_lights, LightingData::MAX_LIGHTS,
                m_statistics.invalid_lights);
        frames.retain_current_frame_resource(frame);
        std::vector<DrawItem> queue;
        queue.reserve(items.size());
        for(const auto& item : items) {
            if(auto material =
                    prepare_material(item.material, frames.get_current_frame_serial()))
                queue.push_back({&item, std::move(material)});
        }
        std::stable_sort(
            queue.begin(), queue.end(), [](const DrawItem& a, const DrawItem& b) {
                return std::tie(a.material->prepared->layout->get_name(),
                           a.item->material.material_handle)
                       < std::tie(b.material->prepared->layout->get_name(),
                           b.item->material.material_handle);
            });
        auto& command = frames.get_current_command_buffer();
        const Pipeline* active_pipeline = nullptr;
        const MaterialResources* active_material = nullptr;
        std::vector<QueueSemaphoreSubmit> waits;
        for(const auto& draw : queue) {
            const auto& material = draw.material;
            const auto& pipeline = material->pipeline->pipeline;
            if(active_pipeline != pipeline.get()) {
                command.bind_pipeline(*pipeline);
                active_pipeline = pipeline.get();
                active_material = nullptr;
                ++m_statistics.pipeline_binds;
            }
            if(active_material != material.get()) {
                const std::array sets{
                    frame->descriptor->get(), material->descriptor->get()};
                command.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                    pipeline->get_layout()->get(), 0, sets, {});
                active_material = material.get();
                ++m_statistics.material_binds;
            }
            frames.retain_current_frame_resource(material);
            frames.retain_current_frame_resource(draw.item->mesh);
            append_wait(waits, draw.item->mesh->get_ready_completion(),
                Flags<PipelineStage>(PipelineStage::VertexInput));
            for(const auto& texture : material->prepared->textures) {
                append_wait(waits, texture.texture->get_ready_completion(),
                    Flags<PipelineStage>(PipelineStage::FragmentShader));
            }
            const PushConstant push{.model = draw.item->model_matrix};
            command.push_constants(*pipeline->get_layout(),
                Flags<ShaderStage>(ShaderStage::Vertex), 0, &push, sizeof(push));
            draw.item->mesh->draw(command);
            ++m_statistics.draw_calls;
        }
        std::erase_if(waits, [](const auto& wait) {
            return wait.semaphore->get_counter_value() >= wait.value;
        });
        std::erase_if(m_materials, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, cached] : m_materials)
            cached.used = false;
        m_prepared.collect_unused();
        std::erase_if(m_unsupported, [&](const auto& entry) {
            return entry.second != frames.get_current_frame_serial();
        });
        m_statistics.cached_material_versions = static_cast<uint32_t>(m_materials.size());
        return waits;
    }
}
