#pragma once
#include "vk_common.h"
#include "vertex_description.h"

namespace Comet {
    class Device;
    class Shader;
    struct ShaderLayout;
    class RenderPass;

    class PipelineLayout {
    public:
        PipelineLayout(Device* device, const ShaderLayout& layout);

        ~PipelineLayout();

        [[nodiscard]] vk::PipelineLayout get() const { return m_pipeline_layout; }

    private:
        Device* m_device;
        vk::PipelineLayout m_pipeline_layout;
    };

    struct PipelineVertexInputState {
        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    };

    struct PipelineInputAssemblyState {
        Topology topology = Topology::TriangleList;
        bool primitive_restart_enable = false;
    };

    struct PipelineRasterizationState {
        bool depth_clamp_enable = false;
        bool rasterizer_discard_enable = false;
        PolygonMode polygon_mode = PolygonMode::Fill;
        CullMode cull_mode = CullMode::None;
        FrontFace front_face = FrontFace::CCW;
        bool depth_bias_enable = false;
        float depth_bias_constant_factor = 0.0f;
        float depth_bias_clamp = 0.0f;
        float depth_bias_slope_factor = 0.0f;
        float line_width = 1.0f;
    };

    struct PipelineMultisampleState {
        SampleCount rasterization_samples = SampleCount::Count1;
        bool sample_shading_enable = false;
        float min_sample_shading = 0.2f;
    };

    struct PipelineDepthStencilState {
        bool depth_test_enable = false;
        bool depth_write_enable = false;
        CompareOp depth_compare_op = CompareOp::Never;
        bool depth_bounds_test_enable = false;
        bool stencil_test_enable = false;
    };

    struct PipelineColorBlendState {
        bool blend_enable = false;
        BlendFactor src_color_blend_factor = BlendFactor::One;
        BlendFactor dst_color_blend_factor = BlendFactor::Zero;
        BlendOp color_blend_op = BlendOp::Add;
        BlendFactor src_alpha_blend_factor = BlendFactor::One;
        BlendFactor dst_alpha_blend_factor = BlendFactor::Zero;
        BlendOp alpha_blend_op = BlendOp::Add;
        Flags<ColorWriteMask> color_write_mask = Flags<ColorWriteMask>(ColorWriteMask::All);
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
        vk::Viewport viewport{};
        vk::Rect2D scissor{};
        vk::PipelineColorBlendAttachmentState color_blend_state{
            vk::False,                  // blendEnable
            vk::BlendFactor::eOne,      // srcColorBlendFactor
            vk::BlendFactor::eZero,     // dstColorBlendFactor
            vk::BlendOp::eAdd,          // colorBlendOp
            vk::BlendFactor::eOne,      // srcAlphaBlendFactor
            vk::BlendFactor::eZero,     // dstAlphaBlendFactor
            vk::BlendOp::eAdd,          // alphaBlendOp
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG 
            | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA  // colorWriteMask
        };
        PipelineDynamicState dynamic_state;

        void set_vertex_input_state(const VertexInputDescription& description);

        void set_input_assembly_state(Topology topology, bool primitive_restart_enable = false);

        void set_rasterization_state(const PipelineRasterizationState& raster_state);

        void set_multisample_state(SampleCount samples, bool sample_shading_enable, float min_sample_shading = 0.f);

        void set_depth_stencil_state(const PipelineDepthStencilState& ds_state);

        void set_color_blend_attachment_state(const PipelineColorBlendState& cb_state);

        void set_dynamic_state(const std::vector<DynamicState>& dy_states);

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
        [[nodiscard]] static std::array<vk::PipelineShaderStageCreateInfo, 2> create_shader_stages(
            const std::shared_ptr<Shader>& vertex_shader, const std::shared_ptr<Shader>& fragment_shader);

        [[nodiscard]] vk::PipelineVertexInputStateCreateInfo create_vertex_input_state() const;

        [[nodiscard]] vk::PipelineInputAssemblyStateCreateInfo create_input_assembly_state() const;

        [[nodiscard]] vk::PipelineRasterizationStateCreateInfo create_rasterization_state() const;

        [[nodiscard]] vk::PipelineMultisampleStateCreateInfo create_multisample_state() const;

        [[nodiscard]] vk::PipelineDepthStencilStateCreateInfo create_depth_stencil_state() const;

        [[nodiscard]] vk::PipelineColorBlendStateCreateInfo create_color_blend_state() const;

        [[nodiscard]] vk::PipelineViewportStateCreateInfo create_viewport_state();

        [[nodiscard]] vk::PipelineDynamicStateCreateInfo create_dynamic_state() const;

        std::string m_name;
        Device* m_device;
        RenderPass* m_render_pass;
        vk::Pipeline m_pipeline;
        std::shared_ptr<PipelineLayout> m_layout;
        PipelineConfig m_config;
    };

    class PipelineManager {
    public:
        PipelineManager(Device* device, RenderPass* render_pass);

        std::shared_ptr<Pipeline> create_pipeline(
            const std::string& name,
            const ShaderLayout& layout,
            const VertexInputDescription& vertex_input,
            const PipelineConfig& config,
            std::shared_ptr<Shader> vert_shader,
            std::shared_ptr<Shader> frag_shader
        );

        [[nodiscard]] std::shared_ptr<Pipeline> get_pipeline(const std::string& name) const;

    private:
        Device* m_device;
        RenderPass* m_render_pass;
        std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_pipelines;
    };
}
