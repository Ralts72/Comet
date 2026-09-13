#include "render/material_renderer.h"

#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/mesh_data.h"
#include "render/resource/resource_manager.h"
#include "render/resource/texture.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace Comet {
    namespace {
        void append_wait(std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
            if(!completion.is_valid())
                return;
            const QueueSemaphoreSubmit candidate(completion, stages);
            const auto found = std::ranges::find_if(
                waits, [&](const auto& wait) { return wait.semaphore == candidate.semaphore; });
            if(found == waits.end()) {
                waits.push_back(candidate);
            } else {
                found->value = std::max(found->value, candidate.value);
                found->stage_mask = found->stage_mask | candidate.stage_mask;
            }
        }
    }

    MaterialRenderer::MaterialRenderer(Device& device, PipelineManager& pipelines,
        ResourceManager& resources, const uint32_t frame_slot_count, const SampleCount samples)
        : m_device(device) {
        m_sampler = resources.get_sampler_manager().get_linear_repeat();
        DescriptorSetLayoutBindings frame_bindings;
        frame_bindings.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        auto frame_layout = DescriptorSetLayout::create(device, frame_bindings);
        if(!frame_layout)
            throw std::runtime_error(
                "Cannot initialize frame descriptor layout: " + frame_layout.error().message);
        m_frame_layout = std::move(frame_layout).value();
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slot_count);
        auto pool_result = DescriptorPool::create(device, frame_slot_count, pool_sizes);
        if(!pool_result)
            throw std::runtime_error(
                "Cannot initialize frame descriptor pool: " + pool_result.error().message);
        std::shared_ptr<DescriptorPool> pool = std::move(pool_result).value();
        auto descriptors = pool->allocate_descriptor_set(*m_frame_layout, frame_slot_count);
        if(!descriptors)
            throw std::runtime_error(
                "Cannot allocate frame descriptor sets: " + descriptors.error().message);
        for(uint32_t slot = 0; slot < frame_slot_count; ++slot) {
            auto frame = std::make_shared<FrameResources>();
            frame->layout = m_frame_layout;
            frame->pool = pool;
            auto buffer =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(ViewProjectMatrix), false, nullptr, "frame view-project");
            if(!buffer)
                throw std::runtime_error(
                    "Cannot initialize frame uniform buffer: " + buffer.error().message);
            frame->buffer = std::move(buffer).value();
            frame->descriptor = descriptors.value()[slot];
            const DescriptorSet::UniformBufferWrite write{
                0, *frame->buffer, sizeof(ViewProjectMatrix)};
            frame->descriptor->update(device, std::span(&write, 1));
            m_frames.push_back(std::move(frame));
        }
        auto& shaders = resources.get_shader_manager();
        auto vertex = shaders.load_shader("material_mesh", MATERIAL_MESH_VERT);
        if(!vertex)
            throw std::runtime_error(
                "Cannot initialize built-in material vertex shader: " + vertex.error().message);
        const auto add_builtin = [&](const std::string& name, std::span<const uint32_t> words,
                                     std::string_view layout_name) {
            auto fragment = shaders.load_shader(name, words);
            if(!fragment)
                return Result<void, GraphicsError>::failure(fragment.error());
            return add_pipeline(pipelines, vertex.value(), fragment.value(),
                MaterialLayout::find_builtin(layout_name), samples);
        };
        if(auto result =
                add_builtin("material_textured", MATERIAL_TEXTURED_FRAG, "unlit_texture_blend");
            !result)
            throw std::runtime_error(
                "Cannot initialize built-in textured material pipeline: " + result.error().message);
        if(auto result = add_builtin("material_solid", MATERIAL_SOLID_FRAG, "unlit_color"); !result)
            throw std::runtime_error(
                "Cannot initialize built-in solid material pipeline: " + result.error().message);
    }

    Result<void, GraphicsError> MaterialRenderer::add_pipeline(PipelineManager& pipelines,
        const std::shared_ptr<Shader>& vertex, const std::shared_ptr<Shader>& fragment,
        std::shared_ptr<const MaterialLayout> layout, const SampleCount samples) {
        if(!layout)
            return Result<void, GraphicsError>::failure({"Missing material layout"});
        if(auto checked = layout->validate(fragment->get_interface()); !checked)
            return Result<void, GraphicsError>::failure({checked.error()});
        auto state = std::make_shared<PipelineState>();
        state->layout = std::move(layout);
        DescriptorSetLayoutBindings bindings;
        if(state->layout->get_parameter_size() > 0) {
            bindings.add_binding(
                0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        }
        for(const auto& texture : state->layout->get_textures()) {
            bindings.add_binding(texture.binding, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        }
        auto material_layout = DescriptorSetLayout::create(m_device, bindings);
        if(!material_layout)
            return Result<void, GraphicsError>::failure(material_layout.error());
        state->material_layout = std::move(material_layout).value();
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {m_frame_layout, state->material_layout};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(PushConstant)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        input.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        input.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_input_assembly_state(Topology::TriangleList);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        config.set_multisample_state(samples, false, 0.2f);
        auto pipeline = pipelines.create_pipeline(
            state->layout->get_name(), shader_layout, config, vertex, fragment);
        if(!pipeline)
            return Result<void, GraphicsError>::failure(pipeline.error());
        state->pipeline = std::move(pipeline).value();
        m_pipelines.emplace(state->layout->get_name(), std::move(state));
        return Result<void, GraphicsError>::success();
    }

    std::shared_ptr<MaterialRenderer::MaterialResources> MaterialRenderer::prepare_material(
        const MaterialBinding& material, const uint64_t frame_serial) {
        if(!material.resource)
            return nullptr;
        const auto pipeline = m_pipelines.find(material.resource->get_template_name());
        if(pipeline == m_pipelines.end()) {
            const auto [entry, inserted] =
                m_unsupported.try_emplace(material.material_handle, frame_serial);
            entry->second = frame_serial;
            if(inserted) {
                LOG_ERROR("Unsupported material layout '{}' for handle {}",
                    material.resource->get_template_name(), material.material_handle.value());
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
        if(cached.resources && cached.resources->prepared == prepared)
            return cached.resources;
        if(cached.failed_candidate == prepared && frame_serial < cached.retry_after_serial) {
            return cached.resources;
        }
        const auto keep_previous = [&](const GraphicsError& error) {
            if(error.is_device_lost())
                throw std::runtime_error("Device lost while preparing material: " + error.message);
            LOG_ERROR("Cannot prepare GPU material for handle {}: {}; previous version {}",
                material.material_handle.value(), error.message,
                cached.resources ? "retained" : "unavailable");
            cached.failed_candidate = prepared;
            cached.retry_after_serial = frame_serial + 60;
            return cached.resources;
        };
        auto candidate = std::make_shared<MaterialResources>();
        candidate->pipeline = pipeline->second;
        candidate->prepared = prepared;
        candidate->sampler = m_sampler;
        DescriptorPoolSizes sizes;
        if(!prepared->parameters.empty()) {
            auto buffer = Buffer::try_create_cpu_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::Uniform), prepared->parameters.size(), true,
                prepared->parameters.data(), "immutable material parameters");
            if(!buffer) {
                return keep_previous(buffer.error());
            }
            candidate->parameters = std::move(buffer).value();
            sizes.add_pool_size(DescriptorType::UniformBuffer, 1);
        }
        if(!prepared->textures.empty()) {
            sizes.add_pool_size(DescriptorType::CombinedImageSampler,
                static_cast<uint32_t>(prepared->textures.size()));
        }
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return keep_previous(pool.error());
        candidate->pool = std::move(pool).value();
        auto sets =
            candidate->pool->allocate_descriptor_set(*candidate->pipeline->material_layout, 1);
        if(!sets)
            return keep_previous(sets.error());
        candidate->descriptor = sets.value().front();
        std::vector<DescriptorSet::UniformBufferWrite> buffers;
        if(candidate->parameters)
            buffers.push_back({0, *candidate->parameters, candidate->parameters->get_size()});
        std::vector<DescriptorSet::ImageSamplerWrite> images;
        images.reserve(prepared->textures.size());
        for(const auto& binding : prepared->textures)
            images.push_back({binding.binding, *binding.texture->get_image_view(), *m_sampler});
        candidate->descriptor->update(m_device, buffers, images);
        cached.resources = candidate;
        cached.failed_candidate.reset();
        ++m_statistics.material_versions_created;
        return candidate;
    }

    std::vector<QueueSemaphoreSubmit> MaterialRenderer::render(FrameScheduler& frames,
        const ViewProjectMatrix& view, const std::span<const ResolvedRenderItem> items) {
        m_statistics = {};
        m_statistics.frame_set_count = static_cast<uint32_t>(m_frames.size());
        const auto& frame = m_frames.at(frames.get_current_frame_slot_index());
        frame->buffer->write(&view);
        frames.retain_current_frame_resource(frame);
        std::vector<DrawItem> queue;
        queue.reserve(items.size());
        for(const auto& item : items) {
            if(auto material = prepare_material(item.material, frames.get_current_frame_serial()))
                queue.push_back({&item, std::move(material)});
        }
        std::stable_sort(queue.begin(), queue.end(), [](const DrawItem& a, const DrawItem& b) {
            return std::tie(
                       a.material->prepared->layout->get_name(), a.item->material.material_handle)
                   < std::tie(
                       b.material->prepared->layout->get_name(), b.item->material.material_handle);
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
                const std::array sets{*frame->descriptor, *material->descriptor};
                command.bind_descriptor_sets(*pipeline->get_layout(), sets);
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
            command.push_constants(*pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Vertex),
                0, &push, sizeof(push));
            draw.item->mesh->draw(command);
            ++m_statistics.draw_calls;
        }
        std::erase_if(waits,
            [](const auto& wait) { return wait.semaphore->get_counter_value() >= wait.value; });
        std::erase_if(m_materials, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, cached] : m_materials)
            cached.used = false;
        m_prepared.collect_unused();
        std::erase_if(m_unsupported,
            [&](const auto& entry) { return entry.second != frames.get_current_frame_serial(); });
        m_statistics.cached_material_versions = static_cast<uint32_t>(m_materials.size());
        return waits;
    }
}
