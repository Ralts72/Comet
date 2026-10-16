#include "render/passes/volumetric_fog_pass.h"
#include "render/material/material_renderer.h"
#include "render/scene/draw_order.h"
#include "render/material/material_programs.h"
#include "render/material/material_layout.h"
#include "render/material/view_lighting.h"
#include "asset/registry.h"
#include "core/geometry.h"
#include "asset/artifact/shader_program_artifact.h"
#include "scene/material_parameters.h"

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
#include "render/resource/irradiance.h"
#include "graphics/resource/image.h"
#include "pbr_instanced_vert.h"
#include "unlit_color_instanced_vert.h"

#include <algorithm>
#include <utility>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string_view>
#include <tuple>

namespace Comet {
    namespace {
        void append_wait(std::vector<QueueSemaphoreSubmit>& waits,
            const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
            if(!completion.is_valid() || completion.is_complete())
                return;
            merge_semaphore_wait(waits, QueueSemaphoreSubmit(completion, stages));
        }
    }

    MaterialRenderer::MaterialRenderer(Device& device, PipelineManager& pipelines,
        const SampleCount samples, MaterialPrograms* programs)
        : m_device(device), m_pipeline_manager(pipelines), m_samples(samples),
          m_programs(programs) {}

    MaterialRenderer::Statistics MaterialRenderer::get_statistics() const {
        auto statistics = m_statistics;
        statistics.pipeline_creations = m_pipeline_manager.get_pipeline_creations();
        statistics.warmup_pipeline_creations = m_warmup_pipeline_creations;
        static_cast<ViewBindings::Statistics&>(statistics) = m_view_bindings.statistics();
        statistics.cached_material_versions = static_cast<uint32_t>(std::ranges::count_if(
            m_materials, [](const auto& entry) { return bool(entry.second.resources); }));
        return statistics;
    }

    void MaterialRenderer::remove_assets(const AssetRegistry::Removals& removals) {
        if(removals.empty())
            return;
        // 注销材质或程序时清理依赖缓存；普通版本替换仍保留失败回退。
        m_prepared.remove_assets(removals);
        std::erase_if(m_materials, [&](const auto& entry) {
            return removals.contains(entry.first.material_handle)
                   || (entry.second.resources
                       && removals.contains(entry.second.resources->pipeline->shader_program));
        });
        std::erase_if(
            m_unsupported, [&](const auto& entry) { return removals.contains(entry.first); });
        std::erase_if(m_project_pipelines, [&](const auto& entry) {
            if(!removals.contains(entry.first.first))
                return false;
            for(const auto& [key, material] : entry.second.materials)
                m_materials.erase(key);
            return true;
        });
        for(auto& [program, active] : m_project_pipelines) {
            bool invalidates_failure = false;
            std::erase_if(active.materials, [&](const auto& entry) {
                if(!removals.contains(entry.first.material_handle))
                    return false;
                if(active.failure_cause == ProjectPipeline::FailureCause::Materials
                    || (active.failure_cause == ProjectPipeline::FailureCause::Overrides
                        && entry.second.overrides))
                    invalidates_failure = true;
                return true;
            });
            if(invalidates_failure)
                active.failed_source.reset();
        }
    }

    Result<std::unique_ptr<MaterialRenderer>, GraphicsError> MaterialRenderer::create(
        Device& device, PipelineManager& pipelines, RenderResources& resources,
        const uint32_t frame_slot_count, const SampleCount samples, const MaterialShaders* shaders,
        MaterialPrograms* programs, std::optional<float> max_anisotropy) {
        auto candidate = std::unique_ptr<MaterialRenderer>(
            new MaterialRenderer(device, pipelines, samples, programs));
        if(auto result = candidate->initialize(
               pipelines, resources, frame_slot_count, samples, shaders, max_anisotropy);
            !result) {
            candidate.reset();
            pipelines.collect_unused();
            return Result<std::unique_ptr<MaterialRenderer>, GraphicsError>::failure(
                result.error());
        }
        return Result<std::unique_ptr<MaterialRenderer>, GraphicsError>::success(
            std::move(candidate));
    }

    Result<void, GraphicsError> MaterialRenderer::initialize(PipelineManager& pipelines,
        RenderResources& resources, const uint32_t frame_slot_count, const SampleCount samples,
        const MaterialShaders* shaders, std::optional<float> max_anisotropy) {
        if(frame_slot_count == 0)
            return Result<void, GraphicsError>::failure({"Material renderer requires frame slots"});
        auto& device = m_device;
        auto sampler = resources.get_sampler_manager().get_linear_repeat(
            max_anisotropy.value_or(device.get_capability().max_sampler_anisotropy));
        if(!sampler)
            return Result<void, GraphicsError>::failure(sampler.error());
        m_sampler = std::move(sampler).value();
        auto white =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        if(!white)
            return Result<void, GraphicsError>::failure(white.error());
        m_white_texture = std::move(white).value();
        // 半精度精确表示 (0.5, 0.5, 1, 1)，默认法线不引入 RGBA8 量化倾斜。
        auto normal = resources.try_create_texture({.width = 1,
            .height = 1,
            .format = Format::R16G16B16A16_SFLOAT,
            .pixels = {0, 0x38, 0, 0x38, 0, 0x3c, 0, 0x3c}});
        if(!normal)
            return Result<void, GraphicsError>::failure(normal.error());
        m_normal_texture = std::move(normal).value();
        if(auto bindings =
                m_view_bindings.initialize(device, resources, frame_slot_count, m_white_texture);
            !bindings)
            return bindings;
        m_instances.resize(frame_slot_count);
        const auto defaults = default_material_shaders();
        auto initial = defaults;
        if(shaders) {
            if(auto checked = validate_material_shaders(*shaders); !checked)
                return Result<void, GraphicsError>::failure({checked.error()});
            merge_material_shaders(initial, *shaders);
        }
        // 固定内嵌程序，不受开发中的 Shader 覆盖影响。
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> error_pipelines;
        ReloadReport error_report;
        if(auto result = prepare_builtin_pipeline(pipelines, {"unlit_color", "unlit_color"},
               defaults.at("unlit_color"), samples, error_pipelines, error_report);
            !result)
            return result;
        const auto& error_pipeline = error_pipelines.at("unlit_color");
        auto error_source = std::make_shared<Material>("Shader error", "unlit_color");
        static_cast<void>(error_source->set_vector_property("color", {1, 0, 1, 1}));
        MaterialRuntimeCache error_cache;
        auto prepared_error =
            error_cache.prepare(INVALID_ASSET_HANDLE, error_source, error_pipeline->layout);
        if(!prepared_error)
            return Result<void, GraphicsError>::failure({prepared_error.error()});
        auto error_material = create_material(prepared_error.value(), error_pipeline, nullptr);
        if(!error_material)
            return Result<void, GraphicsError>::failure(error_material.error());
        m_error_material = std::move(error_material).value();
        auto loaded = reload_shaders(pipelines, initial, samples);
        if(!loaded)
            return Result<void, GraphicsError>::failure(loaded.error());
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_builtin_pipeline(
        PipelineManager& pipelines, const MaterialShaderDefinition& definition,
        const MaterialShaderProgram& code, SampleCount samples,
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>>& candidates,
        ReloadReport& report) {
        const std::string name(definition.name);
        auto vertex = Shader::create(m_device, name, code.vertex, code.vertex_entry);
        if(!vertex)
            return Result<void, GraphicsError>::failure(vertex.error());
        auto fragment = Shader::create(m_device, name, code.fragment, code.fragment_entry);
        if(!fragment)
            return Result<void, GraphicsError>::failure(fragment.error());
        const auto old = m_pipelines.find(std::string(definition.material));
        std::shared_ptr<const MaterialLayout> metadata;
        if(old == m_pipelines.end())
            metadata = MaterialLayout::find_builtin(definition.material);
        else
            metadata = old->second->layout;
        auto reflected = MaterialLayout::reflect(metadata, fragment.value()->get_interface());
        if(!reflected)
            return Result<void, GraphicsError>::failure({reflected.error()});
        std::shared_ptr<DescriptorSetLayout> descriptor_layout;
        if(old != m_pipelines.end() && reflected.value() == old->second->layout)
            descriptor_layout = old->second->material_layout;
        std::shared_ptr<Shader> instanced_vertex;
        // 只配对已知内置顶点程序；开发覆盖可能修改顶点位置，必须使用覆盖本身。
        static const auto builtins = default_material_shaders();
        const bool static_mesh_bounds =
            code.vertex_entry == "main" && code.vertex == builtins.at(name).vertex;
        if(static_mesh_bounds) {
            const auto words = name == "pbr"
                                   ? std::span<const uint32_t>(PBR_INSTANCED_VERT)
                                   : std::span<const uint32_t>(UNLIT_COLOR_INSTANCED_VERT);
            auto shader = Shader::create(m_device, name + " instanced", words);
            if(!shader)
                return Result<void, GraphicsError>::failure(shader.error());
            instanced_vertex = std::move(shader).value();
        }
        auto candidate =
            create_pipeline(pipelines, vertex.value(), fragment.value(), reflected.value(), samples,
                descriptor_layout, INVALID_ASSET_HANDLE, instanced_vertex, static_mesh_bounds);
        if(!candidate)
            return Result<void, GraphicsError>::failure(candidate.error());
        if(old != m_pipelines.end() && old->second->pipeline == candidate.value()->pipeline
            && old->second->instanced_pipeline == candidate.value()->instanced_pipeline)
            candidates.insert_or_assign(std::string(definition.material), old->second);
        else {
            candidates.insert_or_assign(
                std::string(definition.material), std::move(candidate).value());
            ++report.pipelines;
        }
        return Result<void, GraphicsError>::success();
    }

    struct MaterialRenderer::ShaderUpdate::State {
        MaterialRenderer* owner = nullptr;
        ReloadReport report;
        std::unordered_map<std::string, std::shared_ptr<const PipelineState>> pipelines;
        MaterialRuntimeCache prepared;
        std::map<MaterialInstanceKey, CachedMaterial> materials;
    };

    MaterialRenderer::ShaderUpdate::ShaderUpdate(std::unique_ptr<State> state)
        : m_state(std::move(state)) {}
    MaterialRenderer::ShaderUpdate::ShaderUpdate(ShaderUpdate&&) noexcept = default;
    MaterialRenderer::ShaderUpdate& MaterialRenderer::ShaderUpdate::operator=(
        ShaderUpdate&&) noexcept = default;
    MaterialRenderer::ShaderUpdate::~ShaderUpdate() = default;

    const MaterialRenderer::ReloadReport& MaterialRenderer::ShaderUpdate::report() const {
        return m_state->report;
    }

    void MaterialRenderer::ShaderUpdate::publish() && {
        auto state = std::move(m_state);
        if(state->report.pipelines == 0)
            return;
        state->owner->m_pipelines.swap(state->pipelines);
        state->owner->m_prepared.swap(state->prepared);
        state->owner->m_materials.swap(state->materials);
    }

    Result<MaterialRenderer::ReloadReport, GraphicsError> MaterialRenderer::reload_shaders(
        PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples) {
        auto candidate = prepare_shaders(pipelines, shaders, samples);
        if(!candidate)
            return Result<ReloadReport, GraphicsError>::failure(candidate.error());
        const auto report = candidate.value().report();
        std::move(candidate).value().publish();
        return Result<ReloadReport, GraphicsError>::success(report);
    }

    Result<MaterialRenderer::ShaderUpdate, GraphicsError> MaterialRenderer::prepare_shaders(
        PipelineManager& pipelines, const MaterialShaders& shaders, SampleCount samples) {
        using Reload = Result<ShaderUpdate, GraphicsError>;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const auto elapsed = [](Clock::time_point since) {
            return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
        };
        if(auto checked = validate_material_shaders(shaders); !checked)
            return Reload::failure({checked.error()});
        auto state = std::make_unique<ShaderUpdate::State>();
        state->owner = this;
        auto& report = state->report;
        auto& candidates = state->pipelines;
        candidates = m_pipelines;
        for(const auto& definition : builtin_material_shaders()) {
            const auto found = shaders.find(definition.name);
            if(found == shaders.end())
                continue;
            if(auto result = prepare_builtin_pipeline(
                   pipelines, definition, found->second, samples, candidates, report);
                !result)
                return Reload::failure(result.error());
        }
        report.pipeline_preparation_ms = elapsed(start);
        if(report.pipelines == 0)
            return Reload::success(ShaderUpdate(std::move(state)));
        const auto copy_start = Clock::now();
        auto& prepared_candidates = state->prepared;
        auto& material_candidates = state->materials;
        prepared_candidates = m_prepared;
        material_candidates = m_materials;
        report.candidate_copy_ms = elapsed(copy_start);
        for(auto& [handle, cached] : material_candidates) {
            if(!cached.resources || cached.resources->pipeline->shader_program)
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
        return Reload::success(ShaderUpdate(std::move(state)));
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
        prepare_material_update(const AssetHandle handle,
            const std::shared_ptr<const Material>& snapshot,
            const std::shared_ptr<const ShaderProgramArtifact>& program_source) {
        MaterialRenderer* renderer = this;
        auto updates =
            prepare_material_updates(std::span(&renderer, 1), handle, snapshot, program_source);
        if(!updates)
            return Result<MaterialUpdate, GraphicsError>::failure(updates.error());
        return Result<MaterialUpdate, GraphicsError>::success(std::move(updates.value().front()));
    }

    Result<std::vector<MaterialRenderer::MaterialUpdate>, GraphicsError> MaterialRenderer::
        prepare_material_updates(const std::span<MaterialRenderer* const> renderers,
            const AssetHandle handle, const std::shared_ptr<const Material>& snapshot,
            const std::shared_ptr<const ShaderProgramArtifact>& program_source) {
        using Preparation = Result<std::vector<MaterialUpdate>, GraphicsError>;
        if(!handle || !snapshot)
            return Preparation::failure({"Material update requires an identity and source"});
        const auto& source = snapshot;
        if(source->get_shader_program()) {
            if(auto refreshed = refresh_program(renderers, source->get_shader_program(),
                   source->get_template_name(), program_source);
                !refreshed)
                return Preparation::failure(refreshed.error());
        }
        std::vector<MaterialUpdate> updates;
        updates.reserve(renderers.size());
        for(auto* renderer : renderers) {
            auto candidate = renderer->prepare_material_bindings(handle, source);
            if(!candidate)
                return Preparation::failure(candidate.error());
            updates.push_back(std::move(candidate).value());
        }
        return Preparation::success(std::move(updates));
    }

    Result<MaterialRenderer::MaterialUpdate, GraphicsError> MaterialRenderer::
        prepare_material_bindings(
            const AssetHandle handle, const std::shared_ptr<const Material>& material) {
        using Preparation = Result<MaterialUpdate, GraphicsError>;
        auto pipeline = find_pipeline(*material);
        if(!pipeline && material->get_shader_program()) {
            const auto program = m_project_pipelines.find(
                {material->get_shader_program(), material->get_template_name()});
            // 编辑缺失贴图等材质输入时可以沿用布局准备修正，绘制仍显示错误材质。
            if(program != m_project_pipelines.end() && program->second.requested_source
                && program->second.failure_cause == ProjectPipeline::FailureCause::Materials)
                pipeline = program->second.pipeline;
        }
        if(!pipeline)
            return Preparation::failure({"Material Shader program is not available"});
        MaterialUpdate update;
        update.m_owner = this;
        update.m_handle = handle;
        update.m_source = material;
        auto prepared = update.m_prepared.prepare(handle, material, pipeline->layout);
        if(!prepared)
            return Preparation::failure({prepared.error()});
        std::shared_ptr<MaterialResources> previous;
        if(const auto cached = m_materials.find({handle}); cached != m_materials.end())
            previous = cached->second.resources;
        auto resources = create_material(prepared.value(), pipeline, previous);
        if(!resources)
            return Preparation::failure(resources.error());
        update.m_resources = std::move(resources).value();
        return Preparation::success(std::move(update));
    }

    void MaterialRenderer::MaterialUpdate::publish() && {
        m_owner->m_prepared.merge(std::move(m_prepared));
        auto& cached = m_owner->m_materials[{m_handle}];
        cached = {};
        cached.resources = std::move(m_resources);
        cached.used = true;
        m_owner->m_unsupported.erase(m_handle);
        if(m_source->get_shader_program()) {
            auto& program = m_owner->m_project_pipelines.at(
                {m_source->get_shader_program(), m_source->get_template_name()});
            const auto previous = program.materials.find({m_handle});
            if(program.failure_cause == ProjectPipeline::FailureCause::Materials
                && (previous == program.materials.end() || previous->second.source != m_source
                    || previous->second.revision != m_source->get_revision()))
                program.failed_source.reset();
            program.materials.insert_or_assign(MaterialInstanceKey{m_handle},
                MaterialInput{m_source, m_source->get_revision(), {}});
        }
        m_source.reset();
    }

    Result<std::shared_ptr<const MaterialRenderer::PipelineState>, GraphicsError> MaterialRenderer::
        create_pipeline(PipelineManager& pipelines, const std::shared_ptr<Shader>& vertex,
            const std::shared_ptr<Shader>& fragment, std::shared_ptr<const MaterialLayout> layout,
            const SampleCount samples, std::shared_ptr<DescriptorSetLayout> material_layout,
            const AssetHandle shader_program, const std::shared_ptr<Shader>& instanced_vertex,
            const bool static_mesh_bounds) {
        using Creation = Result<std::shared_ptr<const PipelineState>, GraphicsError>;
        if(!layout)
            return Creation::failure({"Missing material layout"});
        if(auto checked = layout->validate(fragment->get_interface()); !checked)
            return Creation::failure({checked.error()});
        auto state = std::make_shared<PipelineState>();
        state->shader_program = shader_program;
        state->static_mesh_bounds = static_mesh_bounds;
        state->samples = samples;
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
        state->vertex = vertex;
        state->fragment = fragment;
        state->instanced_vertex = instanced_vertex;
        auto defaults = create_surface_pipelines(pipelines, *state, {});
        if(!defaults)
            return Creation::failure(defaults.error());
        state->pipeline = defaults.value().pipeline;
        state->instanced_pipeline = defaults.value().instanced_pipeline;
        return Creation::success(std::move(state));
    }

    Result<MaterialRenderer::SurfacePipelines, GraphicsError> MaterialRenderer::
        create_surface_pipelines(PipelineManager& pipelines, const PipelineState& program,
            const MaterialSurface surface, const bool mirrored) {
        using Creation = Result<SurfacePipelines, GraphicsError>;
        if(!surface.valid())
            return Creation::failure({"Invalid material surface"});
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {m_view_bindings.layout(), program.material_layout};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(PushConstant)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        input.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        input.add_attribute(2, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
        if(std::ranges::any_of(program.vertex->get_interface().get_inputs(),
               [](const auto& attribute) { return attribute.location == 7; }))
            input.add_attribute(7, 0, Format::R32G32B32A32_SFLOAT, offsetof(MeshVertex, tangent));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_input_assembly_state(Topology::TriangleList);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        config.rasterization_state.front_face = mirrored ? FrontFace::CW : FrontFace::CCW;
        config.set_multisample_state(program.samples, false, 0.2f);
        config.rasterization_state.cull_mode =
            surface.double_sided ? CullMode::None : CullMode::Back;
        if(surface.alpha_mode == AlphaMode::Blend) {
            config.enable_alpha_blend();
            config.color_blend_state.alphaBlendOp = vk::BlendOp::eMax;
            config.depth_stencil_state.depth_write_enable = false;
        } else
            config.color_blend_state.colorWriteMask = vk::ColorComponentFlagBits::eR
                                                      | vk::ColorComponentFlagBits::eG
                                                      | vk::ColorComponentFlagBits::eB;
        bool has_mode = false, has_cutoff = false;
        for(const auto& constant :
            program.fragment->get_interface().get_specialization_constants()) {
            if(constant.name == "comet_alpha_mode") {
                config.fragment_specialization.emplace(
                    constant.id, static_cast<uint32_t>(surface.alpha_mode));
                has_mode = true;
            } else if(constant.name == "comet_alpha_cutoff") {
                if(surface.alpha_mode == AlphaMode::Mask)
                    config.fragment_specialization.emplace(constant.id, surface.alpha_cutoff);
                has_cutoff = true;
            } else if(constant.name == "comet_double_sided")
                config.fragment_specialization.emplace(constant.id, surface.double_sided);
        }
        if(surface.alpha_mode == AlphaMode::Mask && (!has_mode || !has_cutoff))
            return Creation::failure({"Alpha Mask Shader must implement common/surface.glsl"});
        SurfacePipelines result;
        auto pipeline = pipelines.create_pipeline(
            program.layout->get_name(), shader_layout, config, program.vertex, program.fragment);
        if(!pipeline)
            return Creation::failure(pipeline.error());
        result.pipeline = std::move(pipeline).value();
        if(program.instanced_vertex && surface.alpha_mode != AlphaMode::Blend) {
            InstanceBuffer::describe(input);
            config.set_vertex_input_state(input);
            auto instanced = pipelines.create_pipeline(program.layout->get_name() + " instanced",
                shader_layout, config, program.instanced_vertex, program.fragment);
            if(!instanced)
                return Creation::failure(instanced.error());
            result.instanced_pipeline = std::move(instanced).value();
        }
        return Creation::success(std::move(result));
    }

    Result<std::shared_ptr<MaterialRenderer::MaterialResources>, GraphicsError> MaterialRenderer::
        prepare_material(const MaterialBinding& material, const uint64_t frame_serial) {
        using Preparation = Result<std::shared_ptr<MaterialResources>, GraphicsError>;
        if(!material.resource)
            return Preparation::success(nullptr);
        const auto& pipeline = find_pipeline(*material.resource);
        if(!pipeline) {
            const auto [entry, inserted] =
                m_unsupported.try_emplace(material.material_handle, frame_serial);
            entry->second = frame_serial;
            if(inserted && !material.program_source
                && (!m_programs
                    || !m_programs->builtin_failed(material.resource->get_template_name()))) {
                LOG_ERROR("Unavailable Shader program for material handle {} and template '{}'",
                    material.material_handle.value(), material.resource->get_template_name());
            }
            return Preparation::success(m_error_material);
        }
        m_unsupported.erase(material.material_handle);
        const auto key = DrawOrder::material_key(material);
        if(key.instance_id)
            m_has_runtime_instances = true;
        auto& cached = m_materials[key];
        const auto revision = material.resource->get_revision();
        const PreparedInputKey input{frame_serial, revision, material.resource.get(),
            material.overrides.get(), pipeline.get()};
        if(cached.used && cached.prepared_input == input)
            return Preparation::success(cached.resources);
        cached.used = true;
        cached.overrides = material.overrides;
        ++m_statistics.material_preparations;
        const auto complete = [&] {
            cached.prepared_input = input;
            return Preparation::success(cached.resources);
        };
        const auto keep_previous = [&](const GraphicsError& error) {
            if(error.is_device_lost())
                return Preparation::failure(error);
            const auto& previous = cached.resources;
            if(cached.preparation_error != error.message) {
                LOG_ERROR(
                    "Cannot prepare material for handle {} instance {}: {}; previous complete "
                    "version {} (template '{}', program {})",
                    material.material_handle.value(), key.instance_id, error.message,
                    previous ? "retained" : "unavailable",
                    previous ? previous->prepared->layout->get_name() : std::string{},
                    previous ? previous->pipeline->shader_program.value() : 0);
                cached.preparation_error = error.message;
            }
            return complete();
        };
        const auto preparation = m_prepared.prepare(
            material.material_handle, material.resource, pipeline->layout, material.overrides);
        if(!preparation)
            return keep_previous({preparation.error()});
        const auto& prepared = preparation.value();
        if(cached.resources && cached.resources->prepared == prepared
            && cached.resources->pipeline == pipeline)
            return complete();
        if(cached.failed_candidate == prepared && cached.failed_pipeline.lock() == pipeline
            && frame_serial < cached.retry_after_serial) {
            return complete();
        }
        auto candidate = create_material(prepared, pipeline, cached.resources);
        if(!candidate) {
            cached.failed_candidate = prepared;
            cached.failed_pipeline = pipeline;
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
        return complete();
    }

    Result<std::shared_ptr<MaterialRenderer::MaterialResources>, GraphicsError> MaterialRenderer::
        create_material(const std::shared_ptr<const PreparedMaterial>& prepared,
            const std::shared_ptr<const PipelineState>& pipeline,
            const std::shared_ptr<MaterialResources>& previous) {
        using Creation = Result<std::shared_ptr<MaterialResources>, GraphicsError>;
        SurfacePipelines draws{pipeline->pipeline, pipeline->instanced_pipeline};
        if(prepared->surface != MaterialSurface{}) {
            auto selected =
                create_surface_pipelines(m_pipeline_manager, *pipeline, prepared->surface);
            if(!selected)
                return Creation::failure(selected.error());
            draws = std::move(selected).value();
        }
        if(previous && previous->prepared == prepared
            && previous->pipeline->material_layout == pipeline->material_layout) {
            auto candidate = std::make_shared<MaterialResources>(*previous);
            candidate->pipeline = pipeline;
            candidate->draws = std::move(draws);
            candidate->mirrored_draws.reset();
            return Creation::success(std::move(candidate));
        }
        auto candidate = std::make_shared<MaterialResources>();
        candidate->pipeline = pipeline;
        candidate->prepared = prepared;
        candidate->draws = std::move(draws);
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
            if(!texture) {
                texture = m_white_texture;
                const auto& properties = prepared->layout->get_textures();
                const auto property = std::ranges::find(
                    properties, binding.binding, &MaterialLayout::TextureProperty::binding);
                if(property != properties.end()
                    && property->fallback == MaterialLayout::TextureProperty::Fallback::Normal)
                    texture = m_normal_texture;
            }
            const auto& sampler = texture->get_sampler() ? texture->get_sampler() : m_sampler;
            images.push_back({binding.binding, *texture->get_image_view(), *sampler});
            candidate->textures.push_back(std::move(texture));
        }
        candidate->descriptor->update(m_device, buffers, images);
        return Creation::success(std::move(candidate));
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_draws(FrameScheduler& frames,
        const RenderSubmission& submission, const RenderGeometry& geometry) {
        clear_draws();
        m_statistics = {};
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_instances.size())
            return Result<void, GraphicsError>::failure({"Invalid material render frame"});
        if(submission.view_project_matrix) {
            const auto& matrices = *submission.view_project_matrix;
            const auto frustum = Frustum::from_view_projection(matrices.projection * matrices.view);
            if(auto queue = prepare_draw_queue(geometry, frames.get_current_frame_serial(),
                   frustum ? &*frustum : nullptr, matrices.view);
                !queue)
                return queue;
            if(auto instances = upload_instances(frames); !instances) {
                m_draw_queue.clear();
                return instances;
            }
            for(const auto& draw : m_draw_queue)
                if(draw.material->prepared->surface.alpha_mode == AlphaMode::Blend)
                    m_transparent_depths.push_back(draw.camera_depth);
        }
        collect_unused_materials(frames.get_current_frame_serial());
        return Result<void, GraphicsError>::success();
    }

    void MaterialRenderer::record_opaque(
        FrameScheduler& frames, std::vector<QueueSemaphoreSubmit>& waits) {
        record_draws(frames,
            std::span(m_draw_queue).first(m_draw_queue.size() - m_transparent_depths.size()),
            waits);
    }

    void MaterialRenderer::record_transparent(FrameScheduler& frames, const size_t first,
        const size_t count, std::vector<QueueSemaphoreSubmit>& waits) {
        const auto offset = m_draw_queue.size() - m_transparent_depths.size();
        record_draws(frames, std::span(m_draw_queue).subspan(offset + first, count), waits);
    }

    Result<void, GraphicsError> MaterialRenderer::upload_instances(FrameScheduler& frames) {
        auto& instances = m_instances.at(frames.get_current_frame_slot_index());
        auto uploaded = instances.upload(m_device, m_instance_transforms);
        if(!uploaded)
            return Result<void, GraphicsError>::failure(uploaded.error());
        m_statistics.instance_upload_bytes = uploaded.value();
        if(!m_instance_transforms.empty()) {
            frames.retain_current_frame_resource(instances.get_buffer());
        }
        return Result<void, GraphicsError>::success();
    }

    void MaterialRenderer::record_draws(FrameScheduler& frames,
        const std::span<const DrawItem> queue, std::vector<QueueSemaphoreSubmit>& waits) {
        const auto slot = frames.get_current_frame_slot_index();
        auto& command = frames.get_current_command_buffer();
        if(!m_instance_transforms.empty())
            m_instances.at(slot).bind(command);
        const Pipeline* active_pipeline = nullptr;
        const MaterialResources* active_material = nullptr;
        const Mesh* active_mesh = nullptr;
        for(const auto& draw : queue) {
            const auto& material = draw.material;
            const bool instanced = draw.instance_count > 1;
            const auto& pipelines = draw.mirrored ? *material->mirrored_draws : material->draws;
            const auto& pipeline = instanced ? pipelines.instanced_pipeline : pipelines.pipeline;
            if(active_pipeline != pipeline.get()) {
                command.bind_pipeline(*pipeline);
                active_pipeline = pipeline.get();
                active_material = nullptr;
                ++m_statistics.pipeline_binds;
            }
            if(active_material != material.get()) {
                const std::array sets{m_view_bindings.descriptor(slot), *material->descriptor};
                command.bind_descriptor_sets(*pipeline->get_layout(), sets);
                active_material = material.get();
                ++m_statistics.material_binds;
                frames.retain_current_frame_resource(material);
                for(const auto& texture : material->textures) {
                    append_wait(waits, texture->get_ready_completion(),
                        Flags<PipelineStage>(PipelineStage::FragmentShader));
                }
            }
            if(active_mesh != draw.item->mesh.get()) {
                active_mesh = draw.item->mesh.get();
                active_mesh->bind(command);
                frames.retain_current_frame_resource(draw.item->mesh);
                append_wait(waits, draw.item->mesh->get_ready_completion(),
                    Flags<PipelineStage>(PipelineStage::VertexInput));
                ++m_statistics.mesh_binds;
            }
            if(instanced)
                ++m_statistics.instanced_draw_calls;
            else {
                const PushConstant push{.model = draw.item->model_matrix};
                command.push_constants(*pipeline->get_layout(),
                    Flags<ShaderStage>(ShaderStage::Vertex), 0, &push, sizeof(push));
            }
            draw.item->mesh->draw(command, draw.instance_count, draw.first_instance);
            ++m_statistics.draw_calls;
            m_statistics.drawn_instances += draw.instance_count;
            m_statistics.drawn_triangles +=
                uint64_t(draw.item->mesh->get_triangle_count()) * draw.instance_count;
        }
    }

    void MaterialRenderer::collect_unused_materials(const uint64_t frame_serial) {
        std::erase_if(m_materials, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, cached] : m_materials)
            cached.used = false;
        m_prepared.collect_unused();
        std::erase_if(
            m_unsupported, [&](const auto& entry) { return entry.second != frame_serial; });
    }
}
