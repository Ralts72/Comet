#include "resource_manager.h"
#include "common/logger.h"

namespace Comet {
    ResourceManager::ResourceManager(Device* device) : m_device(device) {
        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(device);
        
        LOG_INFO("create sampler manager");
        m_sampler_manager = std::make_unique<SamplerManager>(device);

        LOG_INFO("create material manager");
        m_material_manager = std::make_unique<MaterialManager>(device, m_shader_manager.get(), m_sampler_manager.get());
    }

    std::shared_ptr<Texture> ResourceManager::load_texture(const std::string& path) {
        if (m_textures.contains(path)) {
            return m_textures.find(path)->second;
        }
        
        auto texture = std::make_shared<Texture>(m_device, path);
        m_textures[path] = texture;
        return texture;
    }

    std::shared_ptr<Mesh> ResourceManager::create_mesh(const std::vector<Math::Vertex>& vertices, const std::vector<uint32_t>& indices) {
        auto mesh = std::make_shared<Mesh>(m_device, vertices, indices);
        return mesh;
    }

    std::shared_ptr<Texture> ResourceManager::get_texture(const std::string& name) const {
        if (m_textures.contains(name)) {
            return m_textures.find(name)->second;
        }
        return nullptr;
    }

    std::shared_ptr<Mesh> ResourceManager::get_mesh(const std::string& name) const {
        if (m_meshes.contains(name)) {
            return m_meshes.find(name)->second;
        }
        return nullptr;
    }
}

