#pragma once
#include "asset/handle.h"
#include "common/config.h"
#include "common/shader_resources.h"
#include "graphics/buffer.h"
#include "render_scene.h"
#include "render_context.h"
#include "resource_manager.h"
#include "scene_renderer.h"

#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace Comet {
    class AssetRegistry;

    class Renderer {
    public:
        Renderer(const Window& window, const Config::Runtime& config);

        ~Renderer();

        void on_update(float delta_time);

        void on_render(const RenderScene& render_scene, const AssetRegistry& asset_registry);

        using ImGuiRenderDelegate = std::function<void(CommandBuffer&)>;

        void set_on_imgui_render(ImGuiRenderDelegate delegate) {
            m_on_imgui_render = std::move(delegate);
        }

        // 访问器：提供对子系统的访问
        [[nodiscard]] ResourceManager* get_resource_manager() const { return m_resource_manager.get(); }
        [[nodiscard]] SceneRenderer* get_scene_renderer() const { return m_scene_renderer.get(); }
        [[nodiscard]] RenderContext* get_render_context() const { return m_render_context.get(); }

    private:
        void setup_pipeline();

        void setup_resources();

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        ImGuiRenderDelegate m_on_imgui_render;

        std::vector<std::shared_ptr<Buffer>> m_view_project_uniform_buffers;

        ViewProjectMatrix m_view_project_matrix = {
            .view = Math::Mat4{1.0f},
            .projection = Math::Mat4{1.0f}
        };

        std::unordered_set<AssetHandle> m_missing_mesh_handles;
        std::unordered_set<AssetHandle> m_missing_material_handles;
        std::unordered_set<AssetHandle> m_invalid_material_handles;
        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
