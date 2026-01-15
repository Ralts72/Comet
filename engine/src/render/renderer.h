#pragma once
#include "render_context.h"
#include "resource_manager.h"
#include "scene_renderer.h"
#include "graphics/buffer.h"
#include "mesh.h"
#include "texture.h"
#include "common/shader_resources.h"
#include <memory>
#include <functional>

namespace Comet {
    class Renderer {
    public:
        explicit Renderer(const Window& window);

        ~Renderer();

        void on_update(float delta_time);

        void on_render();

        // 渲染回调：在主渲染后、end_frame 前调用（用于 ImGui 等叠加层）
        using RenderCallback = std::function<void(CommandBuffer&)>;
        void set_overlay_callback(RenderCallback callback) { m_overlay_callback = std::move(callback); }

        // 访问器：提供对子系统的访问
        [[nodiscard]] ResourceManager* get_resource_manager() const { return m_resource_manager.get(); }
        [[nodiscard]] SceneRenderer* get_scene_renderer() const { return m_scene_renderer.get(); }
        [[nodiscard]] RenderContext* get_render_context() const { return m_render_context.get(); }

    private:
        void setup_pipeline();

        void setup_descriptor_sets();

        void setup_resources();

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        RenderCallback m_overlay_callback;

        // 应用层资源（这些应该由应用层管理，但为了向后兼容暂时保留）
        std::shared_ptr<Buffer> m_view_project_uniform_buffer;
        std::shared_ptr<Buffer> m_model_uniform_buffer;
        std::shared_ptr<Texture> m_texture1;
        std::shared_ptr<Texture> m_texture2;
        std::shared_ptr<Mesh> m_cube_mesh;

        // 应用层业务逻辑（这些应该由应用层管理）
        ViewProjectMatrix m_view_project_matrix = {
            .view = Math::Mat4{1.0f},
            .projection = Math::Mat4{1.0f}
        };

        ModelMatrix m_model_matrix = {
            .model = Math::Mat4{1.0f}
        };

        static float total_time;
    };
}
