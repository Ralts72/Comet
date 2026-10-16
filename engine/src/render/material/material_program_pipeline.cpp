#include "render/material/material_renderer.h"
#include "render/material/material_programs.h"
#include "render/material/material_layout.h"
#include "render/material/material.h"
#include "render/scene/draw_order.h"
#include "render/scene/render_submission.h"
#include "asset/artifact/shader_program_artifact.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/pipeline.h"
#include "diagnostics/logger.h"
#include "common/scope_exit.h"

#include <algorithm>
#include <utility>

namespace Comet {
    void MaterialRenderer::sync_world_inputs(const RenderWorld& world) {
        bool requested_instances = false;
        for(const auto& item : world.render_items) {
            const auto& material = item.material;
            if(!material.resource)
                continue;
            const auto key = DrawOrder::material_key(material);
            if(key.instance_id) {
                requested_instances = true;
                if(const auto cached = m_materials.find(key); cached != m_materials.end())
                    cached->second.requested_input = &item;
            }
            if(!material.resource->get_shader_program())
                continue;
            const auto program = std::pair{
                material.resource->get_shader_program(), material.resource->get_template_name()};
            auto& active = m_project_pipelines[program];
            active.requested_source = material.program_source;
            // 借用本次提交的最后一个输入；同步后不保留提交指针。
            active.materials[key].requested_input = &material;
        }
        if(requested_instances || m_has_runtime_instances)
            sync_runtime_instances();
        m_has_runtime_instances = requested_instances;
        sync_program_inputs();
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_programs(const RenderWorld& world) {
        MaterialRenderer* renderer = this;
        return prepare_programs(std::span(&renderer, 1), world);
    }

    Result<void, GraphicsError> MaterialRenderer::prepare_programs(
        const std::span<MaterialRenderer* const> renderers, const RenderWorld& world) {
        if(renderers.empty())
            return Result<void, GraphicsError>::success();
        for(auto* renderer : renderers)
            renderer->sync_world_inputs(world);
        for(const auto& [key, requested] : renderers.front()->m_project_pipelines) {
            if(requested.materials.empty())
                continue;
            if(auto prepared =
                    refresh_program(renderers, key.first, key.second, requested.requested_source);
                !prepared)
                return prepared;
        }
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> MaterialRenderer::warmup_scene(const RenderWorld& world) {
        const auto before = m_pipeline_manager.get_pipeline_creations();
        const ScopeExit count_created([&] {
            m_warmup_pipeline_creations = m_pipeline_manager.get_pipeline_creations() - before;
        });
        // 与绘制相同的材质身份；重复输入沿用最后一份快照。
        std::map<MaterialInstanceKey, std::pair<const MaterialBinding*, bool>> inputs;
        for(const auto& item : world.render_items) {
            if(!item.material.resource)
                continue;
            auto& input = inputs[DrawOrder::material_key(item.material)];
            input.first = &item.material;
            input.second |= DrawOrder::reverses_winding(item);
        }
        for(const auto& [key, input] : inputs) {
            auto material = prepare_material(*input.first, 0);
            if(!material)
                return Result<void, GraphicsError>::failure(material.error());
            if(!material.value() || !input.second || material.value()->mirrored_draws)
                continue;
            auto mirrored = create_surface_pipelines(m_pipeline_manager,
                *material.value()->pipeline, material.value()->prepared->surface, true);
            if(!mirrored)
                return Result<void, GraphicsError>::failure(mirrored.error());
            material.value()->mirrored_draws = std::move(mirrored).value();
        }
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> MaterialRenderer::refresh_program(
        const std::span<MaterialRenderer* const> renderers, const AssetHandle handle,
        const std::string& template_name,
        const std::shared_ptr<const ShaderProgramArtifact>& source) {
        using Preparation = Result<void, GraphicsError>;
        if(renderers.empty())
            return Preparation::success();
        auto* programs = renderers.front()->m_programs;
        if(!programs)
            return Preparation::success();
        const auto key = std::pair{handle, template_name};
        for(auto* renderer : renderers)
            renderer->m_project_pipelines[key].requested_source = source;
        const auto publish =
            [&](const std::shared_ptr<const ShaderProgramArtifact>& version) -> Preparation {
            struct Candidate {
                MaterialRenderer* owner;
                std::optional<ProjectUpdate> update;
            };
            std::vector<Candidate> candidates;
            std::shared_ptr<const MaterialLayout> layout;
            for(auto* renderer : renderers) {
                auto& active = renderer->m_project_pipelines[key];
                if(active.source == version) {
                    layout = active.pipeline->layout;
                    continue;
                }
                if(active.source && MaterialPrograms::same_content(*active.source, *version)) {
                    candidates.push_back({renderer, std::nullopt});
                    layout = active.pipeline->layout;
                    continue;
                }
                auto prepared =
                    renderer->prepare_project_pipeline(handle, template_name, active, version);
                if(!prepared) {
                    if(prepared.error().is_device_lost())
                        return Preparation::failure(prepared.error());
                    const auto cause = active.failure_cause;
                    for(auto* rejected : renderers) {
                        auto& state = rejected->m_project_pipelines[key];
                        state.failed_source = version;
                        state.failure_cause = cause;
                    }
                    LOG_ERROR(
                        "Cannot publish project Shader program {} for '{}': {}; showing error material",
                        handle.value(), template_name, prepared.error().message);
                    return Preparation::failure(prepared.error());
                }
                layout = prepared.value().pipeline->layout;
                candidates.push_back({renderer, std::move(prepared).value()});
            }
            for(auto& candidate : candidates) {
                auto& active = candidate.owner->m_project_pipelines[key];
                if(candidate.update) {
                    candidate.owner->m_prepared.swap(candidate.update->prepared);
                    candidate.owner->m_materials.swap(candidate.update->materials);
                    active.pipeline = std::move(candidate.update->pipeline);
                }
                active.source = version;
                active.failed_source.reset();
            }
            programs->publish(handle, template_name, version, std::move(layout));
            return Preparation::success();
        };
        // 新建或重建的目标先恢复上次共同发布的版本，再尝试最新产物。
        if(const auto* prior = programs->published(handle, template_name);
            prior && prior->source != source) {
            const bool missing = std::ranges::any_of(renderers, [&](const auto* renderer) {
                return !renderer->m_project_pipelines.at(key).pipeline;
            });
            if(missing) {
                auto restored = publish(prior->source);
                if(!restored && restored.error().is_device_lost())
                    return restored;
            }
        }
        if(!source)
            return Preparation::success();
        const bool settled = std::ranges::all_of(renderers, [&](const auto* renderer) {
            const auto& active = renderer->m_project_pipelines.at(key);
            return active.source == source || active.failed_source == source;
        });
        if(settled)
            return Preparation::success();
        auto installed = publish(source);
        if(!installed && installed.error().is_device_lost())
            return installed;
        return Preparation::success();
    }

    void MaterialRenderer::sync_runtime_instances() {
        // 只消费本次输入；移除项的在途资源仍由帧保活。
        for(auto cached = m_materials.begin(); cached != m_materials.end();) {
            const auto& key = cached->first;
            if(!key.instance_id) {
                ++cached;
                continue;
            }
            auto& current = cached->second;
            const auto* input = std::exchange(current.requested_input, nullptr);
            if(!input) {
                m_prepared.erase(key);
                cached = m_materials.erase(cached);
                continue;
            }
            const auto& material = input->material;
            if(current.resources && current.overrides != material.overrides) {
                // 事务重绑必须读取当前快照，而不是失败候选看到的旧覆盖。
                if(auto refreshed = m_prepared.prepare(key.material_handle, material.resource,
                       current.resources->pipeline->layout, material.overrides);
                    refreshed)
                    current.overrides = material.overrides;
            }
            ++cached;
        }
    }

    void MaterialRenderer::sync_program_inputs() {
        for(auto& [program, active] : m_project_pipelines) {
            bool materials_changed = false;
            bool overrides_changed = false;
            for(auto entry = active.materials.begin(); entry != active.materials.end();) {
                auto& input = entry->second;
                const auto* requested = std::exchange(input.requested_input, nullptr);
                if(!requested) {
                    materials_changed = true;
                    overrides_changed |= bool(input.overrides);
                    entry = active.materials.erase(entry);
                    continue;
                }
                const auto revision = requested->resource->get_revision();
                materials_changed |=
                    input.source != requested->resource || input.revision != revision;
                overrides_changed |= input.overrides != requested->overrides;
                if(input.source != requested->resource)
                    input.source = requested->resource;
                input.revision = revision;
                if(input.overrides != requested->overrides)
                    input.overrides = requested->overrides;
                ++entry;
            }
            const bool retry = (active.failure_cause == ProjectPipeline::FailureCause::Materials
                                   && materials_changed)
                               || (active.failure_cause == ProjectPipeline::FailureCause::Overrides
                                   && overrides_changed);
            if(retry)
                active.failed_source.reset();
        }
    }

    Result<MaterialRenderer::ProjectUpdate, GraphicsError> MaterialRenderer::
        prepare_project_pipeline(AssetHandle handle, const std::string& template_name,
            ProjectPipeline& active, const std::shared_ptr<const ShaderProgramArtifact>& version) {
        using Preparation = Result<ProjectUpdate, GraphicsError>;
        const auto builtin = m_pipelines.find(template_name);
        if(builtin == m_pipelines.end())
            return Preparation::failure({"Unknown material rendering template: " + template_name});
        active.failure_cause = ProjectPipeline::FailureCause::Shader;
        auto layout = m_programs->describe(*version, template_name, builtin->second->layout);
        if(!layout)
            return Preparation::failure({layout.error()});
        for(const auto& [key, material] : active.materials) {
            if(!material.overrides)
                continue;
            if(auto checked = layout.value()->validate_parameters(*material.overrides); !checked) {
                active.failure_cause = ProjectPipeline::FailureCause::Overrides;
                return Preparation::failure({checked.error()});
            }
        }
        const auto name = "project Shader " + std::to_string(handle.value());
        auto vertex = Shader::create(m_device, name, version->vertex_words, version->vertex_entry);
        if(!vertex)
            return Preparation::failure(vertex.error());
        auto fragment =
            Shader::create(m_device, name, version->fragment_words, version->fragment_entry);
        if(!fragment)
            return Preparation::failure(fragment.error());
        auto candidate =
            create_pipeline(m_pipeline_manager, vertex.value(), fragment.value(), layout.value(),
                m_samples, version->material ? nullptr : builtin->second->material_layout, handle);
        if(!candidate)
            return Preparation::failure(candidate.error());
        auto prepared = m_prepared;
        auto materials = m_materials;
        for(const auto& [material_handle, material] : active.materials) {
            auto& cached = materials[material_handle];
            auto rebound = prepared.prepare(material_handle.material_handle, material.source,
                candidate.value()->layout, material.overrides);
            if(!rebound) {
                active.failure_cause = ProjectPipeline::FailureCause::Materials;
                return Preparation::failure({rebound.error()});
            }
            auto resources = create_material(rebound.value(), candidate.value(), cached.resources);
            if(!resources)
                return Preparation::failure(resources.error());
            cached.resources = std::move(resources).value();
            cached.overrides = material.overrides;
            cached.failed_candidate.reset();
            cached.failed_pipeline.reset();
            cached.preparation_error.clear();
        }
        return Preparation::success(
            {std::move(candidate).value(), std::move(prepared), std::move(materials)});
    }

}
