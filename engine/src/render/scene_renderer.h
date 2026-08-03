#pragma once
#include "asset/handle.h"
#include "common/config.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "frame_manager.h"
#include "graphics/buffer.h"
#include "graphics/descriptor_set.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/sampler.h"
#include "graphics/vertex_description.h"
#include "mesh.h"
#include "render_context.h"
#include "render_target.h"
#include "texture.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Comet {
    class ResourceManager;
    class VertexInputDescription;

    class COMET_API SceneRenderer {
    public:
        enum class RenderMode {
            Runtime,
            SceneView,
            GameView
        };

        SceneRenderer(RenderContext* context, const Config::Vulkan& vulkan_config, const Config::Render& render_config);

        void setup_render_pass();

        std::shared_ptr<DescriptorSetLayout> create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings);

        void setup_pipeline(const ResourceManager* resource_manager,
                            const ShaderLayout& layout,
                            const VertexInputDescription& vertex_input,
                            const PipelineConfig& config);

        [[nodiscard]] const DescriptorSet& prepare_material_descriptor_set(
            AssetHandle material_handle,
            const std::shared_ptr<Buffer>& view_project_buffer,
            const std::shared_ptr<Texture>& texture0,
            const std::shared_ptr<Texture>& texture1,
            SamplerManager* sampler_manager);

        void render_item(const Math::Mat4& model_matrix,
                         const std::shared_ptr<Mesh>& mesh,
                         const DescriptorSet& descriptor_set) const;

        uint32_t begin_frame();

        void end_frame();

        void end_render_pass() const;

        void set_render_mode(RenderMode mode);

        [[nodiscard]] RenderMode get_render_mode() const { return m_render_mode; }
        [[nodiscard]] FrameManager* get_frame_manager() const { return m_frame_manager.get(); }
        [[nodiscard]] RenderTarget* get_render_target() const { return m_render_target.get(); }
        [[nodiscard]] PipelineManager* get_pipeline_manager() const { return m_pipeline_manager.get(); }
        [[nodiscard]] RenderPass* get_render_pass() const { return m_render_pass.get(); }
        [[nodiscard]] std::shared_ptr<Pipeline> get_pipeline() const { return m_pipeline; }
        [[nodiscard]] CommandBuffer& get_current_command_buffer() const;

        void recreate_swapchain();

        using SwapchainRecreateCallback = std::function<void()>;
        void set_swapchain_recreate_callback(SwapchainRecreateCallback callback) {
            m_swapchain_recreate_callback = std::move(callback);
        }

    private:
        struct MaterialDescriptorState {
            std::shared_ptr<DescriptorPool> pool;
            std::vector<DescriptorSet> descriptor_sets;
            std::vector<std::shared_ptr<Buffer>> view_project_buffers;
            std::vector<std::shared_ptr<Texture>> textures0;
            std::vector<std::shared_ptr<Texture>> textures1;
        };

        void update_descriptor_set(const DescriptorSet& descriptor_set,
                                   const std::shared_ptr<Buffer>& view_project_buffer,
                                   const std::shared_ptr<Texture>& texture0,
                                   const std::shared_ptr<Texture>& texture1,
                                   SamplerManager* sampler_manager) const;

        SwapchainRecreateCallback m_swapchain_recreate_callback;
        RenderContext* m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameManager> m_frame_manager;
        std::unique_ptr<RenderTarget> m_render_target;
        RenderMode m_render_mode = RenderMode::Runtime;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::unordered_map<AssetHandle, MaterialDescriptorState> m_material_descriptors;
        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
