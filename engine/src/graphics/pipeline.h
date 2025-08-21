#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    class Shader;
    struct ShaderLayout;
    class RenderPass;

    class PipelineLayout {
    public:
        PipelineLayout(Device* device, const ShaderLayout& layout);

        ~PipelineLayout();

        [[nodiscard]] vk::PipelineLayout get_pipeline_layout() const { return m_pipeline_layout; }

    private:
        Device* m_device;
        vk::PipelineLayout m_pipeline_layout;
    };

    struct PipelineVertexInputState {
        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    };

    struct PipelineInputAssemblyState {
        vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
        vk::Bool32 primitive_restart_enable = VK_FALSE;
    };

    struct PipelineRasterizationState {
        vk::Bool32 depth_clamp_enable = VK_FALSE;
        vk::Bool32 rasterizer_discard_enable = VK_FALSE;
        vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
        vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eNone;
        vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;
        vk::Bool32 depth_bias_enable = VK_FALSE;
        float depth_bias_constant_factor = 0.0f;
        float depth_bias_clamp = 0.0f;
        float depth_bias_slope_factor = 0.0f;
        float line_width = 1.0f;
    };

    struct PipelineMultisampleState {
        vk::SampleCountFlagBits rasterization_samples = vk::SampleCountFlagBits::e1;
        vk::Bool32 sample_shading_enable = VK_FALSE;
        float min_sample_shading = 0.2f;
    };

    struct PipelineDepthStencilState {
        vk::Bool32 depth_test_enable = VK_FALSE;
        vk::Bool32 depth_write_enable = VK_FALSE;
        vk::CompareOp depth_compare_op = vk::CompareOp::eNever;
        vk::Bool32 depth_bounds_test_enable = VK_FALSE;
        vk::Bool32 stencil_test_enable = VK_FALSE;
    };

    struct PipelineColorBlendState {
        vk::PipelineColorBlendAttachmentState color_blend_attachment_state{
            vk::False,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
            | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
    };

    struct PipelineDynamicState {
        std::vector<vk::DynamicState> dynamic_states;
    };

    struct PipelineConfig {
        PipelineVertexInputState vertex_input_state;
        PipelineInputAssemblyState input_assembly_state;
        PipelineRasterizationState rasterization_state;
        PipelineMultisampleState multisample_state;
        PipelineDepthStencilState depth_stencil_state;
        PipelineColorBlendState color_blend_state;
        PipelineDynamicState dynamic_state;

        void set_vertex_input_state(const std::vector<vk::VertexInputBindingDescription>& vertex_bindings,
                                    const std::vector<vk::VertexInputAttributeDescription>& vertex_attrs);

        void set_input_assembly_state(vk::PrimitiveTopology topology, vk::Bool32 primitive_restart_enable = VK_FALSE);

        void set_rasterization_state(const PipelineRasterizationState& raster_state);

        void set_multisample_state(vk::SampleCountFlagBits samples, vk::Bool32 sample_shading_enable, float min_sample_shading = 0.f);

        void set_depth_stencil_state(const PipelineDepthStencilState& ds_state);

        void set_color_blend_attachment_state(const PipelineColorBlendState& cb_state);

        void set_dynamic_state(const std::vector<vk::DynamicState>& dy_states);

        void enable_alpha_blend();

        void enable_depth_test();
    };

    class Pipeline {
    public:
        Pipeline(const std::string& name, Device* device, RenderPass* render_pass,
                 const std::shared_ptr<PipelineLayout>& layout,
                 const std::shared_ptr<Shader>& vertex_shader,
                 const std::shared_ptr<Shader>& fragment_shader,
                 const PipelineConfig& config);

        ~Pipeline();

        [[nodiscard]] vk::Pipeline get() const { return m_pipeline; }
        [[nodiscard]] const std::shared_ptr<PipelineLayout>& get_layout() const { return m_layout; }
        [[nodiscard]] const std::string& get_name() const { return m_name; }

    private:
        [[nodiscard]] std::array<vk::PipelineShaderStageCreateInfo, 2> create_shader_stages(
            const std::shared_ptr<Shader>& vertex_shader, const std::shared_ptr<Shader>& fragment_shader);

        [[nodiscard]] vk::PipelineVertexInputStateCreateInfo create_vertex_input_state();

        [[nodiscard]] vk::PipelineInputAssemblyStateCreateInfo create_input_assembly_state();

        [[nodiscard]] vk::PipelineRasterizationStateCreateInfo create_rasterization_state();

        [[nodiscard]] vk::PipelineMultisampleStateCreateInfo create_multisample_state();

        [[nodiscard]] vk::PipelineDepthStencilStateCreateInfo create_depth_stencil_state();

        [[nodiscard]] vk::PipelineColorBlendStateCreateInfo create_color_blend_state();

        [[nodiscard]] vk::PipelineViewportStateCreateInfo create_viewport_state();

        [[nodiscard]] vk::PipelineDynamicStateCreateInfo create_dynamic_state();

        std::string m_name;
        Device* m_device;
        RenderPass* m_render_pass;
        vk::Pipeline m_pipeline;
        std::shared_ptr<PipelineLayout> m_layout;
        PipelineConfig m_config;
    };
}
