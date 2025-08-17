#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    class Shader;
    class ShaderLayout;
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

    struct PipelineVertexInputState{
        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    };

    struct PipelineInputAssemblyState{
        vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
        vk::Bool32 primitive_restart_enable = VK_FALSE;
    };

    struct PipelineRasterizationState{
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

    struct PipelineMultisampleState{
        vk::SampleCountFlagBits rasterization_samples = vk::SampleCountFlagBits::e1;
        vk::Bool32 sample_shading_enable = VK_FALSE;
        float min_sample_shading = 0.2f;
    };

    struct PipelineDepthStencilState{
        vk::Bool32 depth_test_enable = VK_FALSE;
        vk::Bool32 depth_write_enable = VK_FALSE;
        vk::CompareOp depth_compare_op = vk::CompareOp::eNever;
        vk::Bool32 depth_bounds_test_enable = VK_FALSE;
        vk::Bool32 stencil_test_enable = VK_FALSE;
    };

    struct PipelineDynamicState{
        std::vector<vk::DynamicState> dynamic_states;
    };

    struct PipelineConfig{
        PipelineVertexInputState vertex_input_state;
        PipelineInputAssemblyState input_assembly_state;
        PipelineRasterizationState rasterization_state;
        PipelineMultisampleState multisample_state;
        PipelineDepthStencilState depth_stencil_state;
        vk::PipelineColorBlendAttachmentState color_blend_attachment_state{
                vk::False,
                vk::BlendFactor::eOne,
                vk::BlendFactor::eZero,
                vk::BlendOp::eAdd,
                vk::BlendFactor::eOne,
                vk::BlendFactor::eZero,
                vk::BlendOp::eAdd,
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                | vk::ColorComponentFlagBits::eB| vk::ColorComponentFlagBits::eA
        };
        PipelineDynamicState dynamic_state;

        void set_vertex_input_state(const std::vector<vk::VertexInputBindingDescription>& vertex_bindings,
            const std::vector<vk::VertexInputAttributeDescription>& vertex_attrs);
        void set_input_assembly_state(vk::PrimitiveTopology topology, vk::Bool32 primitive_restart_enable = VK_FALSE);
        void set_rasterization_state(const PipelineRasterizationState& raster_state);
        void set_multisample_state(vk::SampleCountFlagBits samples, vk::Bool32 sample_shading_enable, float min_sample_shading = 0.f);
        void set_depth_stencil_state(const PipelineDepthStencilState& ds_state);
        void set_color_blend_attachment_state(vk::Bool32 blend_enable,
            vk::BlendFactor src_color_blend_factor = vk::BlendFactor::eOne,
            vk::BlendFactor dst_color_blend_factor = vk::BlendFactor::eZero,
            vk::BlendOp color_blend_op = vk::BlendOp::eAdd,
            vk::BlendFactor src_alpha_blend_factor = vk::BlendFactor::eOne,
            vk::BlendFactor dst_alpha_blend_factor = vk::BlendFactor::eZero,
            vk::BlendOp alpha_blend_op = vk::BlendOp::eAdd);
        void set_dynamic_state(const std::vector<vk::DynamicState> &dy_states);
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

        [[nodiscard]] vk::Pipeline get_pipeline() const { return m_pipeline; }
        [[nodiscard]] const std::shared_ptr<PipelineLayout>& get_layout() const { return m_layout; }
        [[nodiscard]] const std::shared_ptr<Shader>& get_vertex_shader() const { return m_vertex_shader; }
        [[nodiscard]] const std::shared_ptr<Shader>& get_fragment_shader() const { return m_fragment_shader; }
        [[nodiscard]] const std::string& get_name() const { return m_name; }

    private:
        std::string m_name;
        Device* m_device;
        RenderPass* m_render_pass;
        vk::Pipeline m_pipeline;
        std::shared_ptr<PipelineLayout> m_layout;
        std::shared_ptr<Shader> m_vertex_shader;
        std::shared_ptr<Shader> m_fragment_shader;
        PipelineConfig m_config;
    };

}