#pragma once
#include "common/export.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/sampler.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <memory>

namespace Comet {
    class UploadManager;

    class COMET_API ResourceManager final : public RenderResourceFactory {
    public:
        explicit ResourceManager(Device& device);
        ~ResourceManager() override;
        
        [[nodiscard]] ShaderManager& get_shader_manager() { return *m_shader_manager; }
        [[nodiscard]] const ShaderManager& get_shader_manager() const { return *m_shader_manager; }
        [[nodiscard]] SamplerManager& get_sampler_manager() { return *m_sampler_manager; }
        [[nodiscard]] const SamplerManager& get_sampler_manager() const { return *m_sampler_manager; }

        [[nodiscard]] std::shared_ptr<Texture> create_texture(
            const TextureData& data) override;
        [[nodiscard]] std::shared_ptr<Mesh> create_mesh(
            const MeshData& data) override;

    private:
        Device& m_device;
        std::unique_ptr<UploadManager> m_upload_manager;
        std::unique_ptr<ShaderManager> m_shader_manager;
        std::unique_ptr<SamplerManager> m_sampler_manager;
    };
}
