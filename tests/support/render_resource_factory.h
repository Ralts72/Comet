#pragma once

#include "render/resource/resource_factory.h"
#include "asset/data/mesh_data.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace Comet::Tests {
    // Mesh／Texture 是身份占位符，只能比较身份，不能解引用或打印对象内容。
    class FakeRenderResourceFactory final: public RenderResourceFactory {
    public:
        GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
            const TextureData&) override {
            ++m_texture_creation_count;
            if(m_on_texture_creation) {
                auto callback = std::exchange(m_on_texture_creation, {});
                callback();
            }
            if(m_fail_texture_creation) {
                return GpuResourceResult<std::shared_ptr<Texture>>::failure(m_failure_result);
            }

            auto owner = std::make_shared<std::uint8_t>(0);
            return GpuResourceResult<std::shared_ptr<Texture>>::success(
                std::shared_ptr<Texture>(owner, reinterpret_cast<Texture*>(owner.get())));
        }

        GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(const MeshData& data) override {
            ++m_mesh_creation_count;
            m_last_mesh_vertex_count = data.vertices.size();
            if(m_on_mesh_creation) {
                auto callback = std::exchange(m_on_mesh_creation, {});
                callback();
            }
            if(m_fail_mesh_creation) {
                return GpuResourceResult<std::shared_ptr<Mesh>>::failure(m_failure_result);
            }

            auto owner = std::make_shared<std::uint8_t>(0);
            return GpuResourceResult<std::shared_ptr<Mesh>>::success(
                std::shared_ptr<Mesh>(owner, reinterpret_cast<Mesh*>(owner.get())));
        }

        void fail_mesh_creation(const bool fail) { m_fail_mesh_creation = fail; }

        void fail_texture_creation(const bool fail) { m_fail_texture_creation = fail; }

        void set_failure_result(vk::Result result) { m_failure_result = result; }

        void on_next_mesh_creation(std::function<void()> callback) {
            m_on_mesh_creation = std::move(callback);
        }
        void on_next_texture_creation(std::function<void()> callback) {
            m_on_texture_creation = std::move(callback);
        }

        [[nodiscard]] std::size_t mesh_creation_count() const { return m_mesh_creation_count; }

        [[nodiscard]] std::size_t last_mesh_vertex_count() const {
            return m_last_mesh_vertex_count;
        }

        [[nodiscard]] std::size_t texture_creation_count() const {
            return m_texture_creation_count;
        }

    private:
        vk::Result m_failure_result = vk::Result::eErrorOutOfDeviceMemory;
        bool m_fail_mesh_creation = false;
        bool m_fail_texture_creation = false;
        std::size_t m_mesh_creation_count = 0;
        std::size_t m_last_mesh_vertex_count = 0;
        std::size_t m_texture_creation_count = 0;
        std::function<void()> m_on_mesh_creation;
        std::function<void()> m_on_texture_creation;
    };

}
