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
#include <chrono>
#include <iterator>
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

    MaterialRenderer::MaterialRenderer(Device& device) : m_device(device) {}

    Result<std::unique_ptr<MaterialRenderer>, GraphicsError> MaterialRenderer::create(
        Device& device, PipelineManager& pipelines, ResourceManager& resources,
        const uint32_t frame_slot_count, const SampleCount samples, const ShaderCode* shaders) {
        auto candidate = std::unique_ptr<MaterialRenderer>(new MaterialRenderer(device));
        if(auto result =
                candidate->initialize(pipelines, resources, frame_slot_count, samples, shaders);
            !result)
            return Result<std::unique_ptr<MaterialRenderer>, GraphicsError>::failure(
                result.error());
        return Result<std::unique_ptr<MaterialRenderer>, GraphicsError>::success(
            std::move(candidate));
    }

    Result<void, GraphicsError> MaterialRenderer::initialize(PipelineManager& pipelines,
        ResourceManager& resources, const uint32_t frame_slot_count, const SampleCount samples,
        const ShaderCode* shaders) {
        if(frame_slot_count == 0)
            return Result<void, GraphicsError>::failure({"Material renderer requires frame slots"});
        auto& device = m_device;
        auto sampler = resources.get_sampler_manager().get_linear_repeat();
        if(!sampler)
            return Result<void, GraphicsError>::failure(sampler.error());
        m_sampler = std::move(sampler).value();
        DescriptorSetLayoutBindings frame_bindings;
        frame_bindings.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        auto frame_layout = DescriptorSetLayout::create(device, frame_bindings);
        if(!frame_layout)
            return Result<void, GraphicsError>::failure(frame_layout.error());
        m_frame_layout = std::move(frame_layout).value();
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slot_count);
        auto pool_result = DescriptorPool::create(device, frame_slot_count, pool_sizes);
        if(!pool_result)
            return Result<void, GraphicsError>::failure(pool_result.error());
        std::shared_ptr<DescriptorPool> pool = std::move(pool_result).value();
        auto descriptors = pool->allocate_descriptor_set(*m_frame_layout, frame_slot_count);
        if(!descriptors)
            return Result<void, GraphicsError>::failure(descriptors.error());
        for(uint32_t slot = 0; slot < frame_slot_count; ++slot) {
            auto frame = std::make_shared<FrameResources>();
            frame->layout = m_frame_layout;
            frame->pool = pool;
            auto buffer =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(ViewProjectMatrix), false, nullptr, "frame view-project");
            if(!buffer)
                return Result<void, GraphicsError>::failure(buffer.error());
            frame->buffer = std::move(buffer).value();
            frame->descriptor = descriptors.value()[slot];
            const DescriptorSet::UniformBufferWrite write{
                0, *frame->buffer, sizeof(ViewProjectMatrix)};
            frame->descriptor->update(device, std::span(&write, 1));
            m_frames.push_back(std::move(frame));
        }
        const ShaderCode embedded{
            std::vector<uint32_t>(std::begin(MATERIAL_MESH_VERT), std::end(MATERIAL_MESH_VERT)),
            std::vector<uint32_t>(
                std::begin(MATERIAL_TEXTURED_FRAG), std::end(MATERIAL_TEXTURED_FRAG)),
            std::vector<uint32_t>(std::begin(MATERIAL_SOLID_FRAG), std::end(MATERIAL_SOLID_FRAG))};
        auto loaded = reload_shaders(pipelines, shaders ? *shaders : embedded, samples);
        if(!loaded)
            return Result<void, GraphicsError>::failure(loaded.error());
        return Result<void, GraphicsError>::success();
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> MaterialRenderer::reload_shaders(
        PipelineManager& pipelines, const ShaderCode& shaders, SampleCount samples) {
        using Reload = Result<ReloadReport, GraphicsError>;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto elapsed = [](Clock::time_point since) {
            return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
        };
        // 内嵌程序是本次构建的固定资源契约，重建 Renderer 时也不能以热更候选建立基线。
        static const auto vertex_contract = ShaderInterface::reflect(MATERIAL_MESH_VERT);
        static const auto textured_contract = ShaderInterface::reflect(MATERIAL_TEXTURED_FRAG);
        static const auto solid_contract = ShaderInterface::reflect(MATERIAL_SOLID_FRAG);
        const auto validate = [](std::span<const uint32_t> words,
                                  const Result<ShaderInterface>& contract, std::string_view name,
                                  std::optional<uint32_t> ignored_set = std::nullopt) {
            if(!contract)
                return Result<void, GraphicsError>::failure({contract.error()});
            const auto candidate = ShaderInterface::reflect(words);
            if(!candidate)
                return Result<void, GraphicsError>::failure({candidate.error()});
            if(!contract.value().has_same_resource_layout(candidate.value(), ignored_set))
                return Result<void, GraphicsError>::failure(
                    {"Shader changed fixed resource layout: " + std::string(name)});
            return Result<void, GraphicsError>::success();
        };
        if(auto checked = validate(shaders.vertex, vertex_contract, "material_mesh"); !checked)
            return Reload::failure(checked.error());
        if(auto checked =
                validate(shaders.textured_fragment, textured_contract, "material_textured", 1);
            !checked)
            return Reload::failure(checked.error());
        if(auto checked = validate(shaders.solid_fragment, solid_contract, "material_solid", 1);
            !checked)
            return Reload::failure(checked.error());
        auto vertex = Shader::create(m_device, "material_mesh", shaders.vertex);
        if(!vertex)
            return Reload::failure(vertex.error());
        ReloadReport report;
        decltype(m_pipelines) candidates;
        const auto add_builtin = [&](const std::string& name, std::span<const uint32_t> words,
                                     std::string_view layout_name) {
            auto fragment = Shader::create(m_device, name, words);
            if(!fragment)
                return Result<void, GraphicsError>::failure(fragment.error());
            const auto old = m_pipelines.find(std::string(layout_name));
            const auto metadata = old == m_pipelines.end()
                                      ? MaterialLayout::find_builtin(layout_name)
                                      : old->second->layout;
            auto reflected = MaterialLayout::reflect(metadata, fragment.value()->get_interface());
            if(!reflected)
                return Result<void, GraphicsError>::failure({reflected.error()});
            std::shared_ptr<DescriptorSetLayout> descriptor_layout;
            if(old != m_pipelines.end() && reflected.value() == old->second->layout)
                descriptor_layout = old->second->material_layout;
            auto candidate = create_pipeline(pipelines, vertex.value(), fragment.value(),
                reflected.value(), samples, descriptor_layout);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            if(old != m_pipelines.end() && old->second->pipeline == candidate.value()->pipeline)
                candidates.emplace(layout_name, old->second);
            else {
                candidates.emplace(layout_name, std::move(candidate).value());
                ++report.pipelines;
            }
            return Result<void, GraphicsError>::success();
        };
        if(auto result =
                add_builtin("material_textured", shaders.textured_fragment, "unlit_texture_blend");
            !result)
            return Reload::failure(result.error());
        if(auto result = add_builtin("material_solid", shaders.solid_fragment, "unlit_color");
            !result)
            return Reload::failure(result.error());
        report.pipeline_preparation_ms = elapsed(start);
        if(report.pipelines == 0)
            return Reload::success(report);
        const auto copy_start = Clock::now();
        auto prepared_candidates = m_prepared;
        auto material_candidates = m_materials;
        report.candidate_copy_ms = elapsed(copy_start);
        for(auto& [handle, cached] : material_candidates) {
            if(!cached.resources)
                continue;
            const auto& pipeline = candidates.at(cached.resources->prepared->layout->get_name());
            if(pipeline == cached.resources->pipeline)
                continue;
            const auto cpu_start = Clock::now();
            auto prepared = prepared_candidates.rebind(handle, pipeline->layout);
            report.material_cpu_ms += elapsed(cpu_start);
            if(!prepared)
                return Reload::failure({prepared.error()});
            const auto gpu_start = Clock::now();
            auto resources = create_material(prepared.value(), pipeline, cached.resources);
            report.material_gpu_ms += elapsed(gpu_start);
            if(!resources)
                return Reload::failure(resources.error());
            if(resources.value()->pool != cached.resources->pool)
                ++report.material_bindings;
            cached.resources = std::move(resources).value();
            cached.failed_candidate.reset();
            cached.failed_pipeline.reset();
            cached.preparation_error.clear();
            ++report.material_versions;
        }
        m_pipelines.swap(candidates);
        m_prepared.swap(prepared_candidates);
        m_materials.swap(material_candidates);
        return Reload::success(report);
    }

    std::vector<std::shared_ptr<const MaterialLayout>> MaterialRenderer::get_material_layouts()
        const {
        std::vector<std::shared_ptr<const MaterialLayout>> result;
        result.reserve(m_pipelines.size());
        for(const auto& [name, pipeline] : m_pipelines)
            result.push_back(pipeline->layout);
        return result;
    }

    Result<std::shared_ptr<const MaterialRenderer::PipelineState>, GraphicsError> MaterialRenderer::
        create_pipeline(PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            const SampleCount samples, std::shared_ptr<DescriptorSetLayout> material_layout) {
        using Creation = Result<std::shared_ptr<const PipelineState>, GraphicsError>;
        if(!layout)
            return Creation::failure({"Missing material layout"});
        if(auto checked = layout->validate(fragment->get_interface()); !checked)
            return Creation::failure({checked.error()});
        auto state = std::make_shared<PipelineState>();
        state->layout = std::move(layout);
        if(!material_layout) {
            DescriptorSetLayoutBindings bindings;
            if(state->layout->get_parameter_size() > 0) {
                bindings.add_binding(state->layout->get_parameter_binding(),
                    DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
            }
            for(const auto& texture : state->layout->get_textures()) {
                bindings.add_binding(texture.binding, DescriptorType::CombinedImageSampler,
                    Flags<ShaderStage>(ShaderStage::Fragment));
            }
            auto created = DescriptorSetLayout::create(m_device, bindings);
            if(!created)
                return Creation::failure(created.error());
            material_layout = std::move(created).value();
        }
        state->material_layout = std::move(material_layout);
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
            return Creation::failure(pipeline.error());
        state->pipeline = std::move(pipeline).value();
        return Creation::success(std::move(state));
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
        auto& cached = m_materials[material.material_handle];
        cached.used = true;
        const auto keep_previous = [&](const GraphicsError& error) {
            if(error.is_device_lost())
                throw std::runtime_error("Device lost while preparing material: " + error.message);
            const auto previous = cached.resources && cached.resources->pipeline == pipeline->second
                                      ? cached.resources
                                      : nullptr;
            if(cached.preparation_error != error.message) {
                LOG_ERROR("Cannot prepare material for handle {}: {}; previous version {}",
                    material.material_handle.value(), error.message,
                    previous ? "retained" : "unavailable");
                cached.preparation_error = error.message;
            }
            return previous;
        };
        const auto preparation = m_prepared.prepare(
            material.material_handle, material.resource, pipeline->second->layout);
        if(!preparation)
            return keep_previous({preparation.error()});
        const auto& prepared = preparation.value();
        if(cached.resources && cached.resources->prepared == prepared
            && cached.resources->pipeline == pipeline->second)
            return cached.resources;
        if(cached.failed_candidate == prepared && cached.failed_pipeline.lock() == pipeline->second
            && frame_serial < cached.retry_after_serial) {
            if(cached.resources && cached.resources->pipeline == pipeline->second)
                return cached.resources;
            return nullptr;
        }
        auto candidate = create_material(prepared, pipeline->second, cached.resources);
        if(!candidate) {
            cached.failed_candidate = prepared;
            cached.failed_pipeline = pipeline->second;
            cached.retry_after_serial = frame_serial + 60;
            return keep_previous(candidate.error());
        }
        if(!cached.resources || cached.resources->pool != candidate.value()->pool)
            ++m_statistics.material_bindings_created;
        cached.resources = std::move(candidate).value();
        cached.failed_candidate.reset();
        cached.failed_pipeline.reset();
        cached.preparation_error.clear();
        ++m_statistics.material_versions_created;
        return cached.resources;
    }

    Result<std::shared_ptr<MaterialRenderer::MaterialResources>, GraphicsError> MaterialRenderer::
        create_material(const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous) {
        using Creation = Result<std::shared_ptr<MaterialResources>, GraphicsError>;
        if(previous && previous->prepared == prepared
            && previous->pipeline->material_layout == pipeline->material_layout) {
            auto candidate = std::make_shared<MaterialResources>(*previous);
            candidate->pipeline = pipeline;
            return Creation::success(std::move(candidate));
        }
        auto candidate = std::make_shared<MaterialResources>();
        candidate->pipeline = pipeline;
        candidate->prepared = prepared;
        candidate->sampler = m_sampler;
        DescriptorPoolSizes sizes;
        if(!prepared->parameters.empty()) {
            auto buffer = Buffer::try_create_cpu_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::Uniform), prepared->parameters.size(), true,
                prepared->parameters.data(), "immutable material parameters");
            if(!buffer) {
                return Creation::failure(buffer.error());
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
            return Creation::failure(pool.error());
        candidate->pool = std::move(pool).value();
        auto sets =
            candidate->pool->allocate_descriptor_set(*candidate->pipeline->material_layout, 1);
        if(!sets)
            return Creation::failure(sets.error());
        candidate->descriptor = sets.value().front();
        std::vector<DescriptorSet::UniformBufferWrite> buffers;
        if(candidate->parameters)
            buffers.push_back({prepared->layout->get_parameter_binding(), *candidate->parameters,
                candidate->parameters->get_size()});
        std::vector<DescriptorSet::ImageSamplerWrite> images;
        images.reserve(prepared->textures.size());
        for(const auto& binding : prepared->textures)
            images.push_back({binding.binding, *binding.texture->get_image_view(), *m_sampler});
        candidate->descriptor->update(m_device, buffers, images);
        return Creation::success(std::move(candidate));
    }

    std::vector<QueueSemaphoreSubmit> MaterialRenderer::render(FrameScheduler& frames,
        const std::optional<ViewProjectMatrix>& view,
        const std::span<const ResolvedRenderItem> items) {
        m_statistics = {};
        m_statistics.frame_set_count = static_cast<uint32_t>(m_frames.size());
        std::vector<QueueSemaphoreSubmit> waits;
        if(view) {
            const auto& frame = m_frames.at(frames.get_current_frame_slot_index());
            frame->buffer->write(&*view);
            frames.retain_current_frame_resource(frame);
            std::vector<DrawItem> queue;
            queue.reserve(items.size());
            for(const auto& item : items) {
                if(auto material =
                        prepare_material(item.material, frames.get_current_frame_serial()))
                    queue.push_back({&item, std::move(material)});
            }
            std::stable_sort(queue.begin(), queue.end(), [](const DrawItem& a, const DrawItem& b) {
                return std::tie(a.material->prepared->layout->get_name(),
                           a.item->material.material_handle)
                       < std::tie(b.material->prepared->layout->get_name(),
                           b.item->material.material_handle);
            });
            auto& command = frames.get_current_command_buffer();
            const Pipeline* active_pipeline = nullptr;
            const MaterialResources* active_material = nullptr;
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
                command.push_constants(*pipeline->get_layout(),
                    Flags<ShaderStage>(ShaderStage::Vertex), 0, &push, sizeof(push));
                draw.item->mesh->draw(command);
                ++m_statistics.draw_calls;
            }
            std::erase_if(waits,
                [](const auto& wait) { return wait.semaphore->get_counter_value() >= wait.value; });
        }
        std::erase_if(m_materials, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, cached] : m_materials)
            cached.used = false;
        m_prepared.collect_unused();
        std::erase_if(m_unsupported,
            [&](const auto& entry) { return entry.second != frames.get_current_frame_serial(); });
        m_statistics.cached_material_versions = static_cast<uint32_t>(std::ranges::count_if(
            m_materials, [](const auto& entry) { return bool(entry.second.resources); }));
        return waits;
    }
}
