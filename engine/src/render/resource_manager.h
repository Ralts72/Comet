#pragma once
#include "graphics/device.h"
#include "graphics/shader.h"
#include "graphics/sampler.h"
#include "texture.h"
#include "mesh.h"
#include <memory>
#include <unordered_map>
#include <string>

namespace Comet {
    class ResourceManager {
    public:
        explicit ResourceManager(Device* device);
        
        [[nodiscard]] ShaderManager* get_shader_manager() const { return m_shader_manager.get(); }
        [[nodiscard]] SamplerManager* get_sampler_manager() const { return m_sampler_manager.get(); }
        
        std::shared_ptr<Texture> load_texture(const std::string& path);
        std::shared_ptr<Mesh> create_mesh(const std::vector<Math::Vertex>& vertices, const std::vector<uint32_t>& indices);
        
        [[nodiscard]] std::shared_ptr<Texture> get_texture(const std::string& name) const;
        [[nodiscard]] std::shared_ptr<Mesh> get_mesh(const std::string& name) const;
        
    private:
        Device* m_device;
        std::unique_ptr<ShaderManager> m_shader_manager;
        std::unique_ptr<SamplerManager> m_sampler_manager;
        std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
        std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshes;
    };
}

