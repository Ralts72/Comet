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

    std::shared_ptr<Texture> ResourceManager::create_texture(const TextureData& data) {
        return Texture::create(m_device, *m_upload_manager, data);
    }

    std::shared_ptr<Mesh> ResourceManager::create_mesh(const MeshData& data) {
        return Mesh::create(m_device, *m_upload_manager, data);
    }

    GpuResourceResult<std::shared_ptr<Texture>> ResourceManager::try_create_texture(
        const TextureData& data) {
        return Texture::try_create(m_device, *m_upload_manager, data, true);
    }

    GpuResourceResult<std::shared_ptr<Mesh>> ResourceManager::try_create_mesh(
        const MeshData& data) {
        return Mesh::try_create(m_device, *m_upload_manager, data, true);
    }

    void ResourceManager::collect_completed_uploads() {
        m_upload_manager->collect_completed();
    }

}
