#include "render/material_shader_reload.h"

#include "diagnostics/logger.h"
#include "render/material/material_shader.h"
#include "render/renderer.h"

#include <utility>

namespace CometEditor {
    namespace {
        ShaderReload::Requests material_requests(const std::filesystem::path& shader_root) {
            ShaderReload::Requests requests;
            for(const auto& program : Comet::builtin_material_shaders()) {
                const auto name = std::string(program.name);
                requests.emplace(
                    name + ".vert", Comet::ShaderCompiler::Request{
                                        .source = shader_root / "material" / (name + ".vert"),
                                        .stage = Comet::ShaderStage::Vertex});
                requests.emplace(
                    name + ".frag", Comet::ShaderCompiler::Request{
                                        .source = shader_root / "material" / (name + ".frag"),
                                        .stage = Comet::ShaderStage::Fragment});
            }
            return requests;
        }
    }

    MaterialShaderReload::MaterialShaderReload(Comet::TaskScheduler& scheduler,
        const std::filesystem::path& shader_root, const std::chrono::milliseconds quiet_period)
        : m_reload(scheduler, material_requests(shader_root), shader_root, quiet_period) {
        if(!m_reload.uses_native_notifications())
            LOG_WARN("Built-in shader monitor is using periodic fallback checks");
    }

    Comet::Result<bool, Comet::Error> MaterialShaderReload::update(Comet::Renderer& renderer) {
        using Result = Comet::Result<bool, Comet::Error>;
        const auto compilation = m_reload.update();
        if(!compilation)
            return Result::success(false);
        if(!compilation->succeeded) {
            LOG_ERROR("Material Shader compilation failed; previous version retained: {}",
                compilation->diagnostics);
            return Result::success(false);
        }
        const auto& stages = compilation->stages;
        Comet::MaterialShaders shaders;
        for(const auto& program : Comet::builtin_material_shaders()) {
            const auto name = std::string(program.name);
            shaders.emplace(name, Comet::MaterialShaderProgram{stages.at(name + ".vert").words,
                                      stages.at(name + ".frag").words});
        }
        auto result = renderer.reload_material_shaders(std::move(shaders));
        if(!result) {
            if(result.error().is_device_lost())
                return Result::failure(result.error().as_error());
            if(result.error().is_out_of_memory()) {
                if(!m_reload.retry_delivery(compilation->revision)) {
                    LOG_ERROR(
                        "Material Shader publication retries exhausted; previous version retained, waiting for a new request: {}",
                        result.error().message);
                } else if(m_reported_retry != compilation->revision) {
                    LOG_WARN(
                        "Material Shader publication ran out of memory; previous version retained, retrying: {}",
                        result.error().message);
                    m_reported_retry = compilation->revision;
                }
            } else {
                LOG_ERROR("Material Shader publication failed; previous version retained: {}",
                    result.error().message);
            }
            return Result::success(false);
        }
        m_reported_retry = 0;
        if(!compilation->diagnostics.empty())
            LOG_WARN("{}", compilation->diagnostics);
        if(result.value().pipelines == 0)
            return Result::success(false);
        LOG_INFO(
            "Published material Shader revision {} ({} stages compiled): {} pipelines, {} material versions, {} bindings",
            compilation->revision, compilation->compiled_stages, result.value().pipelines,
            result.value().material_versions, result.value().material_bindings);
        LOG_INFO("Shader preparation: pipelines {:.2f} ms, candidate copies {:.2f} ms, "
                 "material CPU {:.2f} ms, material GPU {:.2f} ms",
            result.value().pipeline_preparation_ms, result.value().candidate_copy_ms,
            result.value().material_cpu_ms, result.value().material_gpu_ms);
        return Result::success(true);
    }
}
