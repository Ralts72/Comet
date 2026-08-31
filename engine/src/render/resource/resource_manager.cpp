#include "render/resource/resource_manager.h"
#include "diagnostics/logger.h"
#include "graphics/command/upload_manager.h"

namespace Comet {
    ResourceManager::ResourceManager(Device& device) : m_device(device) {
        LOG_INFO("create upload manager");
        m_upload_manager = std::make_unique<UploadManager>(device);

        LOG_INFO("create shader manager");
        m_shader_manager = std::make_unique<ShaderManager>(device);
        
        LOG_INFO("create sampler manager");
        m_sampler_manager = std::make_unique<SamplerManager>(device);
    }

    ResourceManager::~ResourceManager() = default;

    std::shared_ptr<Texture> ResourceManager::create_texture(
        const TextureData& data) {
        return std::make_shared<Texture>(m_device, *m_upload_manager, data);
    }

    std::shared_ptr<Mesh> ResourceManager::create_mesh(
        const MeshData& data) {
        return std::make_shared<Mesh>(m_device, *m_upload_manager, data);
    }

    void ResourceManager::collect_completed_uploads() {
        m_upload_manager->collect_completed();
    }

}
