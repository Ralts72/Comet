#pragma once
#include "config/config.h"
#include "render_context.h"
#include "render/scene/render_scene.h"
#include "render/scene/scene_resolver.h"
#include "render/resource/resource_manager.h"
#include "render/scene/scene_renderer.h"

#include <functional>
#include <memory>

namespace Comet {
    class AssetRegistry;

    class COMET_API Renderer {
    public:
        Renderer(const Window& window, const Config& config,
            const AssetRegistry& asset_registry);

        ~Renderer();

        void on_render(const RenderScene& render_scene);

        void enable_offscreen_rendering(Math::Vec2u initial_size);

        void resize_offscreen_target(Math::Vec2u size) const;

        using ImGuiRenderDelegate = std::function<void(CommandBuffer&)>;

        void set_on_imgui_render(ImGuiRenderDelegate delegate) {
            m_on_imgui_render = std::move(delegate);
        }

        [[nodiscard]] ResourceManager& get_resource_manager() {
            return *m_resource_manager;
        }
        [[nodiscard]] const ResourceManager& get_resource_manager() const {
            return *m_resource_manager;
        }
        [[nodiscard]] SceneRenderer& get_scene_renderer() { return *m_scene_renderer; }
        [[nodiscard]] const SceneRenderer& get_scene_renderer() const {
            return *m_scene_renderer;
        }
        [[nodiscard]] RenderContext& get_render_context() { return *m_render_context; }
        [[nodiscard]] const RenderContext& get_render_context() const {
            return *m_render_context;
        }

    private:
        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        SceneResolver m_scene_resolver;
        ImGuiRenderDelegate m_on_imgui_render;
    };
}
