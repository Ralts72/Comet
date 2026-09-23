#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "common/result.h"
#include "render/material/material_shader.h"

#include <map>
#include <memory>
#include <string>

namespace Comet {
    class AssetRegistry;
    class MaterialLayout;
    class ShaderProgramArtifact;

    // 跨 RenderPass/目标重建保留已通过 GPU 发布的程序版本；不拥有目标相关 Pipeline。
    class COMET_API MaterialPrograms {
    public:
        struct Published {
            std::shared_ptr<const ShaderProgramArtifact> source;
            std::shared_ptr<const MaterialLayout> layout;
        };

        explicit MaterialPrograms(const AssetRegistry& assets) : m_assets(assets) {}

        [[nodiscard]] std::shared_ptr<const ShaderProgramArtifact> latest(AssetHandle handle) const;
        [[nodiscard]] const Published* published(
            AssetHandle handle, const std::string& template_name) const;
        [[nodiscard]] Result<std::shared_ptr<const MaterialLayout>> describe(
            const ShaderProgramArtifact& source, const std::string& template_name,
            const std::shared_ptr<const MaterialLayout>& builtin) const;
        [[nodiscard]] static bool same_content(
            const ShaderProgramArtifact& left, const ShaderProgramArtifact& right);
        void publish(AssetHandle handle, std::string template_name,
            std::shared_ptr<const ShaderProgramArtifact> source,
            std::shared_ptr<const MaterialLayout> layout);
        void collect_removed();
        [[nodiscard]] const MaterialShaders* builtin_overrides() const;
        void publish_builtin(MaterialShaders shaders);

    private:
        const AssetRegistry& m_assets;
        std::map<std::pair<AssetHandle, std::string>, Published> m_published;
        MaterialShaders m_builtin_overrides;
    };
}
