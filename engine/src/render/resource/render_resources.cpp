#include "render/resource/render_resources.h"
#include "asset/data/texture_data.h"
#include "asset/data/mesh_data.h"
#include "graphics/resource/sampler.h"
#include "diagnostics/logger.h"
#include "graphics/command/upload_manager.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"

namespace Comet {
    RenderResources::RenderResources(Device& device) : m_device(device) {
        LOG_INFO("create upload manager");
        m_upload_manager = std::make_unique<UploadManager>(device);

        LOG_INFO("create sampler manager");
        m_sampler_manager = std::make_unique<SamplerManager>(device);
    }

    RenderResources::~RenderResources() = default;

    GpuResourceResult<std::shared_ptr<Texture>> RenderResources::try_create_texture(
        const TextureData& data) {
        return Texture::try_create(m_device, *m_upload_manager, data, true);
    }

    GpuResourceResult<std::shared_ptr<Mesh>> RenderResources::try_create_mesh(
        const MeshData& data) {
        return Mesh::try_create(m_device, *m_upload_manager, data, true);
    }

    void RenderResources::collect_completed_uploads() {
        m_upload_manager->collect_completed();
    }

}
