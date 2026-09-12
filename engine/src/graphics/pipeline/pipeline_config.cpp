#include "graphics/pipeline/pipeline_config.h"

#include "graphics/pipeline/vertex_description.h"
#include "graphics/convert.h"

#include <utility>

namespace Comet {
    void PipelineConfig::set_vertex_input_state(
        const VertexInputDescription& description) {
        vertex_input_state.vertex_bindings = description.get_bindings();
        vertex_input_state.vertex_attributes = description.get_attributes();
    }

    void PipelineConfig::set_input_assembly_state(
        const Topology topology, const bool primitive_restart_enable) {
        input_assembly_state.topology = topology;
        input_assembly_state.primitive_restart_enable = primitive_restart_enable;
    }

    void PipelineConfig::set_rasterization_state(
        const PipelineRasterizationState& raster_state) {
        rasterization_state = raster_state;
    }

    void PipelineConfig::set_multisample_state(const SampleCount samples,
        const bool sample_shading_enable, const float min_sample_shading) {
        multisample_state.rasterization_samples = samples;
        multisample_state.sample_shading_enable = sample_shading_enable;
        multisample_state.min_sample_shading = min_sample_shading;
    }

    void PipelineConfig::set_depth_stencil_state(
        const PipelineDepthStencilState& ds_state) {
        depth_stencil_state = ds_state;
    }

    void PipelineConfig::set_color_blend_attachment_state(
        const PipelineColorBlendState& cb_state) {
        color_blend_state.blendEnable = cb_state.blend_enable;
        color_blend_state.srcColorBlendFactor =
            Graphics::blend_factor_to_vk(cb_state.src_color_blend_factor);
        color_blend_state.dstColorBlendFactor =
            Graphics::blend_factor_to_vk(cb_state.dst_color_blend_factor);
        color_blend_state.colorBlendOp =
            Graphics::blend_op_to_vk(cb_state.color_blend_op);
        color_blend_state.srcAlphaBlendFactor =
            Graphics::blend_factor_to_vk(cb_state.src_alpha_blend_factor);
        color_blend_state.dstAlphaBlendFactor =
            Graphics::blend_factor_to_vk(cb_state.dst_alpha_blend_factor);
        color_blend_state.alphaBlendOp =
            Graphics::blend_op_to_vk(cb_state.alpha_blend_op);
        color_blend_state.colorWriteMask =
            Graphics::color_write_mask_to_vk(cb_state.color_write_mask);
    }

    void PipelineConfig::set_dynamic_state(const std::vector<DynamicState>& dy_states) {
        std::vector<vk::DynamicState> dynamic_states;
        dynamic_states.reserve(dy_states.size());
        for(const auto& dy_state : dy_states) {
            dynamic_states.push_back(Graphics::dynamic_state_to_vk(dy_state));
        }
        dynamic_state.dynamic_states = std::move(dynamic_states);
    }

    void PipelineConfig::enable_alpha_blend() {
        color_blend_state.blendEnable = VK_TRUE;
        color_blend_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_blend_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_state.colorBlendOp = vk::BlendOp::eAdd;
        color_blend_state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        color_blend_state.alphaBlendOp = vk::BlendOp::eAdd;
    }

    void PipelineConfig::enable_depth_test() {
        depth_stencil_state.depth_test_enable = VK_TRUE;
        depth_stencil_state.depth_write_enable = VK_TRUE;
        depth_stencil_state.depth_compare_op = CompareOp::Less;
    }
}
