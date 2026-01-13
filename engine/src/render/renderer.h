#pragma once
#include "render_context.h"
#include "resource_manager.h"
#include "scene_renderer.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline.h"
#include "graphics/descriptor_set.h"
#include "graphics/buffer.h"
#include "mesh.h"
#include "texture.h"
#include "common/shader_resources.h"
#include <memory>

namespace Comet {
    class Renderer {
    public:
        explicit Renderer(const Window& window);

        ~Renderer();

        void on_update(float delta_time);

        void on_render();

    private:
        void create_render_pass();
        void setup_pipeline();
        void setup_descriptor_sets();
        void setup_resources();

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        std::shared_ptr<RenderPass> m_render_pass;

        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorPool> m_descriptor_pool;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::vector<DescriptorSet> m_descriptor_sets;

        std::shared_ptr<Buffer> m_view_project_uniform_buffer;
        std::shared_ptr<Buffer> m_model_uniform_buffer;

        std::shared_ptr<Texture> m_texture1;
        std::shared_ptr<Texture> m_texture2;
        std::shared_ptr<Mesh> m_cube_mesh;

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
