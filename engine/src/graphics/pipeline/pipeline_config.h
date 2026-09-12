#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/pipeline/shader_interface.h"

#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <vector>

namespace Comet {
    class VertexInputDescription;

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
}
