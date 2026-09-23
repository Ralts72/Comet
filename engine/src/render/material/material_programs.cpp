#include "render/material/material_programs.h"

#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "graphics/pipeline/shader_interface.h"
#include "render/material/material_layout.h"
#include "render/material/material_shader.h"

#include <algorithm>
#include <utility>

namespace Comet {
    std::shared_ptr<const ShaderProgramArtifact> MaterialPrograms::latest(
        const AssetHandle handle) const {
        return m_assets.resolve<ShaderProgramArtifact>(handle);
    }

    const MaterialPrograms::Published* MaterialPrograms::published(
        const AssetHandle handle, const std::string& template_name) const {
        const auto found = m_published.find({handle, template_name});
        if(found == m_published.end())
            return nullptr;
        return &found->second;
    }

    Result<std::shared_ptr<const MaterialLayout>> MaterialPrograms::describe(
        const ShaderProgramArtifact& source, const std::string& template_name,
        const std::shared_ptr<const MaterialLayout>& builtin) const {
        using Layout = Result<std::shared_ptr<const MaterialLayout>>;
        if(!builtin || builtin->get_name() != template_name)
            return Layout::failure("Unknown material rendering template: " + template_name);
        if(auto checked = validate_material_shaders(
               {{template_name, {source.vertex_words, source.fragment_words, source.vertex_entry,
                                    source.fragment_entry}}});
            !checked)
            return Layout::failure(checked.error());
        auto shader = ShaderInterface::reflect(source.fragment_words, source.fragment_entry);
        if(!shader)
            return Layout::failure(shader.error());
        if(source.material)
            return MaterialLayout::from_program(template_name, *source.material, shader.value());
        auto reflected = MaterialLayout::reflect(builtin, shader.value());
        if(!reflected)
            return reflected;
        if(reflected.value() != builtin)
            return Layout::failure(
                "Project Shader changes the selected template's property layout");
        return reflected;
    }

    bool MaterialPrograms::same_content(
        const ShaderProgramArtifact& left, const ShaderProgramArtifact& right) {
        return left.vertex_words == right.vertex_words
               && left.fragment_words == right.fragment_words
               && left.vertex_entry == right.vertex_entry
               && left.fragment_entry == right.fragment_entry && left.material == right.material;
    }

    void MaterialPrograms::publish(const AssetHandle handle, std::string template_name,
        std::shared_ptr<const ShaderProgramArtifact> source,
        std::shared_ptr<const MaterialLayout> layout) {
        m_published.insert_or_assign(
            {handle, std::move(template_name)}, Published{std::move(source), std::move(layout)});
    }

    void MaterialPrograms::collect_removed() {
        std::erase_if(m_published, [&](const auto& entry) { return !latest(entry.first.first); });
    }

    const MaterialShaders* MaterialPrograms::builtin_overrides() const {
        if(m_builtin_overrides.empty())
            return nullptr;
        return &m_builtin_overrides;
    }

    void MaterialPrograms::publish_builtin(MaterialShaders shaders) {
        merge_material_shaders(m_builtin_overrides, std::move(shaders));
    }
}
