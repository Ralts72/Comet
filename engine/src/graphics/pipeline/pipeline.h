#pragma once
#include "common/export.h"
#include "graphics/pipeline/pipeline_config.h"
#include "graphics/pipeline/pipeline_key.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Comet {
    class Device;
    class Shader;
    struct ShaderLayout;
    class RenderPass;

    class PipelineLayout {
    public:
        PipelineLayout(Device& device, const ShaderLayout& layout);

        ~PipelineLayout();

        PipelineLayout(const PipelineLayout&) = delete;

        PipelineLayout& operator=(const PipelineLayout&) = delete;

        PipelineLayout(PipelineLayout&&) noexcept = delete;

        PipelineLayout& operator=(PipelineLayout&&) noexcept = delete;

        [[nodiscard]] vk::PipelineLayout get() const { return m_pipeline_layout; }

    private:
        Device& m_device;
        vk::PipelineLayout m_pipeline_layout;
    };

    class Pipeline {
    public:
        Pipeline(std::string name, Device& device, RenderPass& render_pass,
            const std::shared_ptr<PipelineLayout>& layout,
            const std::shared_ptr<Shader>& vertex_shader,
            const std::shared_ptr<Shader>& fragment_shader, const PipelineConfig& config);

        ~Pipeline();

        Pipeline(const Pipeline&) = delete;

        Pipeline& operator=(const Pipeline&) = delete;

        Pipeline(Pipeline&&) noexcept = delete;

        Pipeline& operator=(Pipeline&&) noexcept = delete;

        [[nodiscard]] vk::Pipeline get() const { return m_pipeline; }
        [[nodiscard]] const std::shared_ptr<PipelineLayout>& get_layout() const {
            return m_layout;
        }
        [[nodiscard]] const std::string& get_name() const { return m_name; }

    private:
        [[nodiscard]] static std::array<vk::PipelineShaderStageCreateInfo, 2>
        create_shader_stages(const std::shared_ptr<Shader>& vertex_shader,
            const std::shared_ptr<Shader>& fragment_shader);

        [[nodiscard]] static vk::PipelineVertexInputStateCreateInfo
        create_vertex_input_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineInputAssemblyStateCreateInfo
        create_input_assembly_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineRasterizationStateCreateInfo
        create_rasterization_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineMultisampleStateCreateInfo
        create_multisample_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineDepthStencilStateCreateInfo
        create_depth_stencil_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineColorBlendStateCreateInfo
        create_color_blend_state(const PipelineConfig& config);

        [[nodiscard]] static vk::PipelineViewportStateCreateInfo create_viewport_state(
            const vk::Viewport& viewport, const vk::Rect2D& scissor);

        [[nodiscard]] static vk::PipelineDynamicStateCreateInfo create_dynamic_state(
            const PipelineConfig& config);

        std::string m_name;
        Device& m_device;
        vk::Pipeline m_pipeline;
        std::shared_ptr<PipelineLayout> m_layout;
    };

    class COMET_API PipelineManager {
    public:
        PipelineManager(Device& device, RenderPass& render_pass);

        std::shared_ptr<Pipeline> create_pipeline(const std::string& name,
            const ShaderLayout& layout, const PipelineConfig& config,
            const std::shared_ptr<Shader>& vert_shader,
            const std::shared_ptr<Shader>& frag_shader);

        void collect_unused();
        [[nodiscard]] size_t get_cached_pipeline_count() const {
            return m_pipelines.size();
        }

    private:
        Device& m_device;
        RenderPass& m_render_pass;
        std::unordered_map<PipelineKey, std::weak_ptr<Pipeline>, PipelineKey::Hash>
            m_pipelines;
    };
}
