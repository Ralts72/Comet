#pragma once
#include "common/export.h"
#include "graphics/shader.h"
#include "graphics/sampler.h"
#include "texture.h"
#include "mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Comet {
    class COMET_API ResourceManager {
    public:
        explicit ResourceManager(Device& device);
        ~ResourceManager();
        
        [[nodiscard]] ShaderManager& get_shader_manager() { return *m_shader_manager; }
        [[nodiscard]] const ShaderManager& get_shader_manager() const { return *m_shader_manager; }
        [[nodiscard]] SamplerManager& get_sampler_manager() { return *m_sampler_manager; }
        [[nodiscard]] const SamplerManager& get_sampler_manager() const { return *m_sampler_manager; }

        [[nodiscard]] std::shared_ptr<Texture> create_texture(
            const TextureData& data) const;
        [[nodiscard]] std::shared_ptr<Mesh> create_mesh(
            const std::vector<Math::Vertex>& vertices,
            const std::vector<uint32_t>& indices) const;

    private:
        Device& m_device;
        std::unique_ptr<ShaderManager> m_shader_manager;
        std::unique_ptr<SamplerManager> m_sampler_manager;
    };
}
