#pragma once
#include "graphics/vk_common.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/pipeline/shader_interface.h"

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

    struct PipelineVertexInputState {
        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
        bool operator==(const PipelineVertexInputState&) const = default;
    };

    struct PipelineInputAssemblyState {
        Topology topology = Topology::TriangleList;
        bool primitive_restart_enable = false;
        bool operator==(const PipelineInputAssemblyState&) const = default;
    };

    struct PipelineRasterizationState {
        bool depth_clamp_enable = false;
        bool rasterizer_discard_enable = false;
        PolygonMode polygon_mode = PolygonMode::Fill;
        CullMode cull_mode = CullMode::None;
        FrontFace front_face = FrontFace::CW;
        bool depth_bias_enable = false;
        float depth_bias_constant_factor = 0.0f;
        float depth_bias_clamp = 0.0f;
        float depth_bias_slope_factor = 0.0f;
        float line_width = 1.0f;
        bool operator==(const PipelineRasterizationState&) const = default;
    };

    struct PipelineMultisampleState {
        SampleCount rasterization_samples = SampleCount::Count1;
        bool sample_shading_enable = false;
        float min_sample_shading = 0.2f;
        bool operator==(const PipelineMultisampleState&) const = default;
    };

    struct PipelineDepthStencilState {
        bool depth_test_enable = false;
        bool depth_write_enable = false;
        CompareOp depth_compare_op = CompareOp::Never;
        bool depth_bounds_test_enable = false;
        bool stencil_test_enable = false;
        bool operator==(const PipelineDepthStencilState&) const = default;
    };

    struct PipelineColorBlendState {
        bool blend_enable = false;
        BlendFactor src_color_blend_factor = BlendFactor::One;
        BlendFactor dst_color_blend_factor = BlendFactor::Zero;
        BlendOp color_blend_op = BlendOp::Add;
        BlendFactor src_alpha_blend_factor = BlendFactor::One;
        BlendFactor dst_alpha_blend_factor = BlendFactor::Zero;
        BlendOp alpha_blend_op = BlendOp::Add;
        Flags<ColorWriteMask> color_write_mask =
            Flags<ColorWriteMask>(ColorWriteMask::All);
    };

    struct PipelineDynamicState {
        std::vector<vk::DynamicState> dynamic_states;
        bool operator==(const PipelineDynamicState&) const = default;
    };

    struct COMET_API PipelineConfig {
        PipelineVertexInputState vertex_input_state;
        PipelineInputAssemblyState input_assembly_state;
        PipelineRasterizationState rasterization_state;
        PipelineMultisampleState multisample_state;
        PipelineDepthStencilState depth_stencil_state;
        vk::Viewport viewport{0, 100, 100, -100, 0, 1};
        vk::Rect2D scissor{{0, 0}, {100, 100}};
        vk::PipelineColorBlendAttachmentState color_blend_state{
            vk::False,              // blendEnable
            vk::BlendFactor::eOne,  // srcColorBlendFactor
            vk::BlendFactor::eZero, // dstColorBlendFactor
            vk::BlendOp::eAdd,      // colorBlendOp
            vk::BlendFactor::eOne,  // srcAlphaBlendFactor
            vk::BlendFactor::eZero, // dstAlphaBlendFactor
            vk::BlendOp::eAdd,      // alphaBlendOp
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                | vk::ColorComponentFlagBits::eB
                | vk::ColorComponentFlagBits::eA // colorWriteMask
        };
        PipelineDynamicState dynamic_state;
        uint32_t subpass = 0;
        ShaderInterface::Specialization vertex_specialization;
        ShaderInterface::Specialization fragment_specialization;

        bool operator==(const PipelineConfig&) const = default;

        void set_vertex_input_state(const VertexInputDescription& description);

        void set_input_assembly_state(
            Topology topology, bool primitive_restart_enable = false);

        void set_rasterization_state(const PipelineRasterizationState& raster_state);

        void set_multisample_state(SampleCount samples, bool sample_shading_enable,
            float min_sample_shading = 0.f);

        void set_depth_stencil_state(const PipelineDepthStencilState& ds_state);

        void set_color_blend_attachment_state(const PipelineColorBlendState& cb_state);

        void set_dynamic_state(const std::vector<DynamicState>& dy_states);

        void enable_alpha_blend();

        void enable_depth_test();
    };

    // 仅在当前 Device/RenderPass 缓存域内比较，不是磁盘缓存格式。
    struct COMET_API PipelineKey {
        struct ShaderCode {
            std::vector<uint32_t> words;
            std::string entry_point;
            bool operator==(const ShaderCode&) const = default;
        };
        struct Binding {
            uint32_t binding;
            vk::DescriptorType type;
            uint32_t count;
            vk::ShaderStageFlags stages;
            bool operator==(const Binding&) const = default;
        };
        struct AttachmentFormat {
            Format format;
            SampleCount samples;
            bool operator==(const AttachmentFormat&) const = default;
        };
        struct Hash {
            size_t operator()(const PipelineKey& key) const;
        };

        ShaderCode vertex;
        ShaderCode fragment;
        std::vector<std::vector<Binding>> descriptor_sets;
        std::vector<vk::PushConstantRange> push_constants;
        PipelineConfig config;
        vk::RenderPass render_pass;
        std::vector<AttachmentFormat> attachments;

        PipelineKey(const ShaderLayout& layout, const PipelineConfig& config,
            const Shader& vertex, const Shader& fragment, const RenderPass& render_pass);
        bool operator==(const PipelineKey&) const = default;
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
        create_color_blend_state(
            std::span<const vk::PipelineColorBlendAttachmentState> attachments);

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
