#pragma once
#include "common/config.h"
#include "common/shader_resources.h"
#include "render_context.h"
#include "render_scene.h"
#include "scene_resolver.h"
#include "resource_manager.h"
#include "scene_renderer.h"

#include <functional>
#include <memory>

namespace Comet {
    class AssetRegistry;

    class COMET_API Renderer {
    public:
        Renderer(const Window& window,
                 const Config& config,
                 const AssetRegistry& asset_registry);

        ~Renderer();

        void on_render(const RenderScene& render_scene);

        void enable_viewport_rendering(Math::Vec2u initial_size);

        void request_viewport_resize(Math::Vec2u size) const;

        using ImGuiRenderDelegate = std::function<void(CommandBuffer&)>;

        void set_on_imgui_render(ImGuiRenderDelegate delegate) {
            m_on_imgui_render = std::move(delegate);
        }

        [[nodiscard]] ResourceManager& get_resource_manager() { return *m_resource_manager; }
        [[nodiscard]] const ResourceManager& get_resource_manager() const { return *m_resource_manager; }
        [[nodiscard]] SceneRenderer& get_scene_renderer() { return *m_scene_renderer; }
        [[nodiscard]] const SceneRenderer& get_scene_renderer() const { return *m_scene_renderer; }
        [[nodiscard]] RenderContext& get_render_context() { return *m_render_context; }
        [[nodiscard]] const RenderContext& get_render_context() const { return *m_render_context; }

    private:
        void setup_pipeline();

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        SceneResolver m_scene_resolver;
        ImGuiRenderDelegate m_on_imgui_render;

        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
