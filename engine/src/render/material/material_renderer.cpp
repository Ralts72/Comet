#include "render/material/material_renderer.h"
#include "render/material/material_layout.h"

#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "render/resource/render_resources.h"
#include "render/resource/texture.h"
#include "render/resource/environment.h"
#include "graphics/resource/image.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <tuple>

namespace Comet {
    // 私有帧 UBO，与 common/frame.glsl 对齐；不把相机姿态存入材质属性。
    struct alignas(16) MaterialFrameData {
        ViewProjectMatrix matrices;
        Math::Vec3 camera_position;
        float orthographic;
        Math::Vec3 view_direction;
        float reserved = 0;
    };
    static_assert(sizeof(MaterialFrameData) == 160);
    static_assert(offsetof(MaterialFrameData, camera_position) == 128);
    static_assert(offsetof(MaterialFrameData, orthographic) == 140);
    static_assert(offsetof(MaterialFrameData, view_direction) == 144);

    namespace {
        void append_wait(std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
            if(!completion.is_valid())
                return;
            merge_semaphore_wait(waits, QueueSemaphoreSubmit(completion, stages));
        }
    }

    MaterialRenderer::MaterialRenderer(Device& device) : m_device(device) {}

    Result<std::unique_ptr<MaterialRenderer>, GraphicsError> MaterialRenderer::create(
        Device& device, PipelineManager& pipelines, RenderResources& resources,
        const uint32_t frame_slot_count, const SampleCount samples,
        const MaterialShaders* shaders) {
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
        RenderResources& resources, const uint32_t frame_slot_count, const SampleCount samples,
        const MaterialShaders* shaders) {
        if(frame_slot_count == 0)
            return Result<void, GraphicsError>::failure({"Material renderer requires frame slots"});
        auto& device = m_device;
        auto sampler = resources.get_sampler_manager().get_linear_repeat();
        if(!sampler)
            return Result<void, GraphicsError>::failure(sampler.error());
        m_sampler = std::move(sampler).value();
        auto white =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        if(!white)
            return Result<void, GraphicsError>::failure(white.error());
        m_white_texture = std::move(white).value();
        auto black_cube = resources.try_create_texture({.width = 1,
            .height = 1,
            .format = Format::R16G16B16A16_SFLOAT,
            .pixels = std::vector<uint8_t>(6 * 8),
            .cubemap = true});
        if(!black_cube)
            return Result<void, GraphicsError>::failure(black_cube.error());
        m_empty_environment = std::make_shared<Environment>(Environment{
            black_cube.value(), black_cube.value(), black_cube.value(), m_white_texture});
        SamplerDesc environment_desc{.address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge};
        const auto features =
            device.get_capability()
                .physical_device.getFormatProperties(vk::Format::eR16G16B16A16Sfloat)
                .optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
            environment_desc.mag_filter = Filter::Nearest;
            environment_desc.min_filter = Filter::Nearest;
            environment_desc.mipmap_mode = SamplerMipmapMode::Nearest;
        }
        auto environment_sampler = Sampler::create(device, environment_desc);
        if(!environment_sampler)
            return Result<void, GraphicsError>::failure(environment_sampler.error());
        auto shadow_sampler = resources.get_sampler_manager().get_nearest_clamp();
        if(!shadow_sampler)
            return Result<void, GraphicsError>::failure(shadow_sampler.error());
        DescriptorSetLayoutBindings frame_bindings;
        frame_bindings.add_binding(0, DescriptorType::UniformBuffer,
            Flags<ShaderStage>(ShaderStage::Vertex) | ShaderStage::Fragment);
        frame_bindings.add_binding(
            1, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        frame_bindings.add_binding(
            2, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        for(uint32_t binding = 3; binding <= 5; ++binding)
            frame_bindings.add_binding(binding, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto frame_layout = DescriptorSetLayout::create(device, frame_bindings);
        if(!frame_layout)
            return Result<void, GraphicsError>::failure(frame_layout.error());
        m_frame_layout = std::move(frame_layout).value();
        DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(DescriptorType::UniformBuffer, frame_slot_count * 2);
        pool_sizes.add_pool_size(DescriptorType::CombinedImageSampler, frame_slot_count * 4);
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
            frame->shadow_sampler = shadow_sampler.value();
            frame->environment_sampler = environment_sampler.value();
            auto buffer =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(MaterialFrameData), false, nullptr, "frame camera");
            if(!buffer)
                return Result<void, GraphicsError>::failure(buffer.error());
            frame->buffer = std::move(buffer).value();
            frame->descriptor = descriptors.value()[slot];
            auto lighting =
                Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Uniform),
                    sizeof(LightingData), false, nullptr, "frame lighting");
            if(!lighting)
                return Result<void, GraphicsError>::failure(lighting.error());
            frame->lighting = std::move(lighting).value();
            const std::array writes{
                DescriptorSet::UniformBufferWrite{0, *frame->buffer, sizeof(MaterialFrameData)},
                DescriptorSet::UniformBufferWrite{1, *frame->lighting, sizeof(LightingData)}};
            frame->descriptor->update(device, writes);
            m_frames.push_back(std::move(frame));
        }
        auto initial = default_material_shaders();
        if(shaders) {
            if(auto checked = validate_material_shaders(*shaders); !checked)
                return Result<void, GraphicsError>::failure({checked.error()});
            merge_material_shaders(initial, *shaders);
        }
        auto loaded = reload_shaders(pipelines, initial, samples);
        if(!loaded)
            return Result<void, GraphicsError>::failure(loaded.error());
        return Result<void, GraphicsError>::success();
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> MaterialRenderer::reload_shaders(
        PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples) {
        using Reload = Result<ReloadReport, GraphicsError>;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto elapsed = [](Clock::time_point since) {
            return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
        };
        if(auto checked = validate_material_shaders(shaders); !checked)
            return Reload::failure({checked.error()});
        ReloadReport report;
        auto candidates = m_pipelines;
        const auto add_builtin = [&](const std::shared_ptr<Shader>& vertex, const std::string& name,
                                     std::span<const uint32_t> words,
                                     std::string_view layout_name) {
            auto fragment = Shader::create(m_device, name, words);
            if(!fragment)
                return Result<void, GraphicsError>::failure(fragment.error());
            const auto old = m_pipelines.find(std::string(layout_name));
            std::shared_ptr<const MaterialLayout> metadata;
            if(old == m_pipelines.end())
                metadata = MaterialLayout::find_builtin(layout_name);
            else
                metadata = old->second->layout;
            auto reflected = MaterialLayout::reflect(metadata, fragment.value()->get_interface());
            if(!reflected)
                return Result<void, GraphicsError>::failure({reflected.error()});
            std::shared_ptr<DescriptorSetLayout> descriptor_layout;
            if(old != m_pipelines.end() && reflected.value() == old->second->layout)
                descriptor_layout = old->second->material_layout;
            auto candidate = create_pipeline(
                pipelines, vertex, fragment.value(), reflected.value(), samples, descriptor_layout);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            if(old != m_pipelines.end() && old->second->pipeline == candidate.value()->pipeline)
                candidates.insert_or_assign(std::string(layout_name), old->second);
            else {
                candidates.insert_or_assign(std::string(layout_name), std::move(candidate).value());
                ++report.pipelines;
            }
            return Result<void, GraphicsError>::success();
        };
        for(const auto& definition : builtin_material_shaders()) {
            const auto found = shaders.find(definition.name);
            if(found == shaders.end())
                continue;
            const auto& code = found->second;
            const std::string name(definition.name);
            auto vertex = Shader::create(m_device, name, code.vertex);
            if(!vertex)
                return Reload::failure(vertex.error());
            if(auto result = add_builtin(vertex.value(), name, code.fragment, definition.material);
                !result)
                return Reload::failure(result.error());
        }
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

    Result<MaterialRenderer::MaterialUpdate, GraphicsError> MaterialRenderer::
        prepare_material_update(
            const AssetHandle handle, const std::shared_ptr<const Material>& material) {
        using Preparation = Result<MaterialUpdate, GraphicsError>;
        if(!handle || !material)
            return Preparation::failure({"Material update requires an identity and source"});
        const auto pipeline = m_pipelines.find(material->get_template_name());
        if(pipeline == m_pipelines.end())
            return Preparation::failure({"Material template is not available"});
        MaterialUpdate update;
        update.m_owner = this;
        update.m_handle = handle;
        auto prepared = update.m_prepared.prepare(handle, material, pipeline->second->layout);
        if(!prepared)
            return Preparation::failure({prepared.error()});
        std::shared_ptr<MaterialResources> previous;
        if(const auto cached = m_materials.find(handle); cached != m_materials.end())
            previous = cached->second.resources;
        auto resources = create_material(prepared.value(), pipeline->second, previous);
        if(!resources)
            return Preparation::failure(resources.error());
        update.m_resources = std::move(resources).value();
        return Preparation::success(std::move(update));
    }

    void MaterialRenderer::MaterialUpdate::publish() && {
        m_owner->m_prepared.merge(std::move(m_prepared));
        auto& cached = m_owner->m_materials[m_handle];
        cached = {};
        cached.resources = std::move(m_resources);
        cached.used = true;
        m_owner->m_unsupported.erase(m_handle);
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

    Result<std::shared_ptr<MaterialRenderer::MaterialResources>, GraphicsError> MaterialRenderer::
        prepare_material(const MaterialBinding& material, const uint64_t frame_serial) {
        using Preparation = Result<std::shared_ptr<MaterialResources>, GraphicsError>;
        if(!material.resource)
            return Preparation::success(nullptr);
        const auto pipeline = m_pipelines.find(material.resource->get_template_name());
        if(pipeline == m_pipelines.end()) {
            const auto [entry, inserted] =
                m_unsupported.try_emplace(material.material_handle, frame_serial);
            entry->second = frame_serial;
            if(inserted) {
                LOG_ERROR("Unsupported material layout '{}' for handle {}",
                    material.resource->get_template_name(), material.material_handle.value());
            }
            return Preparation::success(nullptr);
        }
        m_unsupported.erase(material.material_handle);
        auto& cached = m_materials[material.material_handle];
        cached.used = true;
        const auto keep_previous = [&](const GraphicsError& error) {
            if(error.is_device_lost())
                return Preparation::failure(error);
            std::shared_ptr<MaterialResources> previous;
            if(cached.resources && cached.resources->pipeline == pipeline->second)
                previous = cached.resources;
            if(cached.preparation_error != error.message) {
                LOG_ERROR("Cannot prepare material for handle {}: {}; previous version {}",
                    material.material_handle.value(), error.message,
                    previous ? "retained" : "unavailable");
                cached.preparation_error = error.message;
            }
            return Preparation::success(previous);
        };
        const auto preparation = m_prepared.prepare(
            material.material_handle, material.resource, pipeline->second->layout);
        if(!preparation)
            return keep_previous({preparation.error()});
        const auto& prepared = preparation.value();
        if(cached.resources && cached.resources->prepared == prepared
            && cached.resources->pipeline == pipeline->second)
            return Preparation::success(cached.resources);
        if(cached.failed_candidate == prepared && cached.failed_pipeline.lock() == pipeline->second
            && frame_serial < cached.retry_after_serial) {
            if(cached.resources && cached.resources->pipeline == pipeline->second)
                return Preparation::success(cached.resources);
            return Preparation::success(nullptr);
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
        return Preparation::success(cached.resources);
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
        for(const auto& binding : prepared->textures) {
            auto texture = binding.texture;
            if(!texture)
                texture = m_white_texture;
            images.push_back({binding.binding, *texture->get_image_view(), *m_sampler});
            candidate->textures.push_back(std::move(texture));
        }
        candidate->descriptor->update(m_device, buffers, images);
        return Creation::success(std::move(candidate));
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> MaterialRenderer::render(
        FrameScheduler& frames, const RenderSubmission& submission, const LightingData& lighting,
        const std::shared_ptr<ImageView>& shadow_map) {
        const auto& view = submission.view_project_matrix;
        const auto& items = submission.render_items;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frames.size() || !shadow_map)
            return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                {"Invalid material render frame or shadow input"});
        const auto previous_omissions =
            std::pair(m_statistics.excess_lights, m_statistics.invalid_lights);
        m_statistics = {};
        m_statistics.frame_set_count = static_cast<uint32_t>(m_frames.size());
        std::vector<QueueSemaphoreSubmit> waits;
        if(view) {
            const auto& frame = m_frames.at(frames.get_current_frame_slot_index());
            const auto camera_world = Math::inverse(view->view);
            // 引擎标准透视矩阵的该项为 -1，正交为 0；不是任意投影的分类器。
            const MaterialFrameData camera{*view, Math::Vec3(camera_world[3]),
                view->projection[2][3] == 0 ? 1.0f : 0.0f, Math::Vec3(camera_world[2])};
            frame->buffer->write(&camera);
            auto environment = m_empty_environment;
            auto frame_lighting = lighting;
            if(submission.environment.lighting && submission.environment_resource) {
                environment = submission.environment_resource;
                const float rotation = Math::radians(submission.environment.rotation);
                const auto& image = environment->specular->get_image_view()->get_image();
                frame_lighting.environment = {submission.environment.lighting_intensity,
                    float(image->get_info().mip_levels - 1), std::sin(rotation),
                    std::cos(rotation)};
            }
            frame->lighting->write(&frame_lighting);
            if(frame->environment != environment) {
                const std::array writes{
                    DescriptorSet::ImageSamplerWrite{
                        3, *environment->irradiance->get_image_view(), *frame->environment_sampler},
                    DescriptorSet::ImageSamplerWrite{
                        4, *environment->specular->get_image_view(), *frame->environment_sampler},
                    DescriptorSet::ImageSamplerWrite{
                        5, *environment->brdf->get_image_view(), *frame->environment_sampler}};
                frame->descriptor->update(m_device, {}, writes);
                frame->environment = environment;
            }
            for(const auto& texture :
                {environment->irradiance, environment->specular, environment->brdf})
                append_wait(waits, texture->get_ready_completion(),
                    Flags<PipelineStage>(PipelineStage::FragmentShader));
            if(frame->shadow_map != shadow_map) {
                const DescriptorSet::ImageSamplerWrite write{
                    2, *shadow_map, *frame->shadow_sampler};
                frame->descriptor->update(m_device, {}, std::span(&write, 1));
                frame->shadow_map = shadow_map;
            }
            m_statistics.light_count = static_cast<uint32_t>(lighting.light_count);
            m_statistics.excess_lights = static_cast<uint32_t>(lighting.excess_lights);
            m_statistics.invalid_lights = static_cast<uint32_t>(lighting.invalid_lights);
            if((m_statistics.excess_lights || m_statistics.invalid_lights)
                && previous_omissions
                       != std::pair(m_statistics.excess_lights, m_statistics.invalid_lights))
                LOG_WARN("Lighting omitted {} excess and {} invalid lights (limit {})",
                    m_statistics.excess_lights, m_statistics.invalid_lights,
                    LightingData::MAX_LIGHTS);
            frames.retain_current_frame_resource(frame);
            std::vector<DrawItem> queue;
            queue.reserve(items.size());
            for(const auto& item : items) {
                auto material = prepare_material(item.material, frames.get_current_frame_serial());
                if(!material)
                    return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                        material.error());
                if(material.value())
                    queue.push_back({&item, std::move(material).value()});
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
                for(const auto& texture : material->textures) {
                    append_wait(waits, texture->get_ready_completion(),
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
        return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::success(std::move(waits));
    }
}
