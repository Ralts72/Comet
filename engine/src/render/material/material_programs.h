#pragma once

#include "asset/handle.h"
#include "asset/registry.h"
#include "scene/material_parameters.h"
#include "common/export.h"
#include "common/result.h"
#include "render/material/material_shader.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace Comet {
    class AssetRegistry;
    class MaterialLayout;
    class ShaderProgramArtifact;

    // 跨 RenderPass/目标重建保留已通过 GPU 发布的程序版本；不拥有目标相关 Pipeline。
    class COMET_API MaterialPrograms final {
    public:
        MaterialPrograms();
        struct Published {
            std::shared_ptr<const ShaderProgramArtifact> source;
            std::shared_ptr<const MaterialLayout> layout;
        };

        struct Publications {
            uint64_t builtin_revision = 0;
            uint64_t builtin_fingerprint = 0;
            std::map<std::pair<AssetHandle, std::string>, Published> programs;
            [[nodiscard]] const Published* published(
                AssetHandle handle, const std::string& template_name) const;
        };
        using Feedback = std::shared_ptr<const Publications>;
        // GPU owner 交付 CPU 布局反馈；后续发布或清理不修改旧反馈。
        [[nodiscard]] Feedback feedback() const { return m_published; }
        [[nodiscard]] const Published* published(
            AssetHandle handle, const std::string& template_name) const;
        [[nodiscard]] static Result<std::shared_ptr<const MaterialLayout>> describe(
            const ShaderProgramArtifact& source, const std::string& template_name,
            const std::shared_ptr<const MaterialLayout>& builtin);
        [[nodiscard]] static bool same_content(
            const ShaderProgramArtifact& left, const ShaderProgramArtifact& right);
        void publish(AssetHandle handle, std::string template_name,
            std::shared_ptr<const ShaderProgramArtifact> source,
            std::shared_ptr<const MaterialLayout> layout);
        void remove_assets(const AssetRegistry::Removals& removals);
        [[nodiscard]] const MaterialShaders* builtin_overrides() const;
        void publish_builtin(MaterialShaders shaders);
        void invalidate_builtin(std::span<const std::string> programs);
        [[nodiscard]] bool builtin_failed(const std::string& material) const;

    private:
        Publications& writable_publications();
        void update_builtin_fingerprint();
        std::shared_ptr<Publications> m_published = std::make_shared<Publications>();
        MaterialShaders m_builtin_overrides;
        std::set<std::string, std::less<>> m_failed_builtins;
    };

    // Main 校验参数、读取 Inspector 布局，只接纳独立反馈，不借用 GPU 程序状态。
    class COMET_API MaterialProgramValidator final: public MaterialParameterValidator {
    public:
        explicit MaterialProgramValidator(const AssetRegistry& assets) : m_assets(assets) {}
        void accept(MaterialPrograms::Feedback feedback) { m_feedback = std::move(feedback); }
        [[nodiscard]] Result<void> validate(const MaterialOverrides& overrides) const override;
        [[nodiscard]] std::shared_ptr<const ShaderProgramArtifact> latest(AssetHandle handle) const;
        [[nodiscard]] const MaterialPrograms::Published* published(
            AssetHandle handle, const std::string& template_name) const;

    private:
        const AssetRegistry& m_assets;
        MaterialPrograms::Feedback m_feedback;
    };
}
