#include "render/resource/ray_scene_buffers.h"

#include "graphics/device.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/buffer.h"

#include <algorithm>
#include <cstring>

namespace Comet {
    Result<std::shared_ptr<RaySceneBuffers>, GraphicsError> RaySceneBuffers::prepare(Device& device,
        const std::shared_ptr<const RayScene>& scene,
        const std::shared_ptr<RaySceneBuffers>& prior) {
        using Prepared = Result<std::shared_ptr<RaySceneBuffers>, GraphicsError>;
        if(!scene || !scene->geometry || !scene->materials || !scene->lighting || !scene->emitters
            || !scene->environment_map)
            return Prepared::failure({"Invalid ray scene"});
        auto next = std::make_shared<RaySceneBuffers>();
        next->source = scene;
        const auto limits = device.get_capability().physical_device.getProperties().limits;
        const RayScene::Node empty_node{};
        const RayScene::Triangle empty_triangle{};
        const uint32_t empty_emitter = 0;
        const RayScene::Surface empty_surface{};
        const RayScene::Texture empty_texture{};
        const Math::Vec4 empty_texel{};
        const auto* previous = prior ? prior->source.get() : nullptr;
        const bool same_geometry = previous && previous->geometry == scene->geometry;
        const bool same_materials = previous && previous->materials == scene->materials;
        const std::array<bool, 8> reused{same_geometry, same_geometry,
            previous && previous->lighting == scene->lighting, same_geometry && same_materials,
            same_materials, same_materials, same_materials,
            previous && previous->environment_map == scene->environment_map};
        const auto& geometry = *scene->geometry;
        const auto& materials = *scene->materials;
        const auto& environment = *scene->environment_map;
        std::vector<std::byte> environment_bytes;
        if(!reused[7]) {
            environment_bytes.resize(16 + environment.texels.size() * sizeof(Math::Vec4));
            const std::array<uint32_t, 4> extent{environment.size.x, environment.size.y, 0, 0};
            std::memcpy(environment_bytes.data(), extent.data(), sizeof(extent));
            if(!environment.texels.empty())
                std::memcpy(environment_bytes.data() + 16, environment.texels.data(),
                    environment.texels.size() * sizeof(Math::Vec4));
        }
        const std::array<const void*, 8> sources{
            geometry.nodes.empty() ? &empty_node : geometry.nodes.data(),
            geometry.triangles.empty() ? &empty_triangle : geometry.triangles.data(),
            scene->lighting.get(),
            scene->emitters->empty() ? &empty_emitter : scene->emitters->data(),
            materials.surfaces.empty() ? &empty_surface : materials.surfaces.data(),
            materials.textures.empty() ? &empty_texture : materials.textures.data(),
            materials.texels.empty() ? &empty_texel : materials.texels.data(),
            environment_bytes.data()};
        const std::array<size_t, 8> sizes{
            std::max(size_t(1), geometry.nodes.size()) * sizeof(empty_node),
            std::max(size_t(1), geometry.triangles.size()) * sizeof(empty_triangle),
            sizeof(LightingData), std::max(size_t(1), scene->emitters->size()) * sizeof(uint32_t),
            std::max(size_t(1), materials.surfaces.size()) * sizeof(empty_surface),
            std::max(size_t(1), materials.textures.size()) * sizeof(empty_texture),
            std::max(size_t(1), materials.texels.size()) * sizeof(empty_texel),
            16 + environment.texels.size() * sizeof(Math::Vec4)};
        for(size_t i = 0; i < sources.size(); ++i) {
            if(reused[i]) {
                next->buffers[i] = prior->buffers[i];
                continue;
            }
            if(sizes[i] > limits.maxStorageBufferRange)
                return Prepared::failure({"射线场景输入超过设备 storage buffer 限制"});
            auto buffer = Buffer::try_create_cpu_buffer(device,
                Flags<BufferUsage>(BufferUsage::Storage), sizes[i], false, sources[i], "ray scene");
            if(!buffer)
                return Prepared::failure({"Cannot allocate ray scene", buffer.result()});
            next->buffers[i] = std::move(buffer).value();
            next->upload_bytes += sizes[i];
            if(i < 2)
                next->geometry_upload_bytes += sizes[i];
        }
        next->sizes = sizes;
        return Prepared::success(std::move(next));
    }

    void RaySceneBuffers::write(Device& device, const DescriptorSet& descriptor) const {
        std::array<vk::DescriptorBufferInfo, 8> infos;
        std::array<vk::WriteDescriptorSet, 8> writes;
        for(uint32_t i = 0; i < infos.size(); ++i) {
            infos[i] = vk::DescriptorBufferInfo(buffers[i]->get(), 0, sizes[i]);
            writes[i] = {descriptor.get(), i >= 3 ? i + 1 : i, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]};
        }
        device.get().updateDescriptorSets(writes, {});
    }
}
