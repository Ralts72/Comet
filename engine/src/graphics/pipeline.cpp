#include "pipeline.h"
#include "device.h"
#include "shader.h"
#include "render_pass.h"

namespace Comet {
    PipelineLayout::PipelineLayout(Device* device, const ShaderLayout& layout) : m_device(device) {
        vk::PipelineLayoutCreateInfo pipeline_layout_create_info = {};
        pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(layout.descriptor_set_layouts.size());
        pipeline_layout_create_info.pSetLayouts = layout.descriptor_set_layouts.data();
        pipeline_layout_create_info.pushConstantRangeCount = static_cast<uint32_t>(layout.push_constants.size());
        pipeline_layout_create_info.pPushConstantRanges = layout.push_constants.data();

        m_pipeline_layout = m_device->get_device().createPipelineLayout(pipeline_layout_create_info);
        LOG_INFO("Vulkan pipeline layout created successfully");
    }

    PipelineLayout::~PipelineLayout() {
        m_device->get_device().destroyPipelineLayout(m_pipeline_layout);
    }

    void PipelineConfig::set_vertex_input_state(const std::vector<vk::VertexInputBindingDescription>& vertex_bindings,
                                                const std::vector<vk::VertexInputAttributeDescription>& vertex_attrs) {
        vertex_input_state.vertex_bindings = vertex_bindings;
        vertex_input_state.vertex_attributes = vertex_attrs;
    }

    void PipelineConfig::set_input_assembly_state(const vk::PrimitiveTopology topology,
                                                  const vk::Bool32 primitive_restart_enable) {
        input_assembly_state.topology = topology;
        input_assembly_state.primitive_restart_enable = primitive_restart_enable;
    }

    void PipelineConfig::set_rasterization_state(const PipelineRasterizationState& raster_state) {
        rasterization_state = raster_state;
    }

    void PipelineConfig::set_multisample_state(const vk::SampleCountFlagBits samples,
                                               const vk::Bool32 sample_shading_enable, const float min_sample_shading) {
        multisample_state.rasterization_samples = samples;
        multisample_state.sample_shading_enable = sample_shading_enable;
        multisample_state.min_sample_shading = min_sample_shading;
    }

    void PipelineConfig::set_depth_stencil_state(const PipelineDepthStencilState& ds_state) {
        depth_stencil_state = ds_state;
    }

    void PipelineConfig::set_color_blend_attachment_state(const PipelineColorBlendState& cb_state) {
        color_blend_state = cb_state;
    }

    void PipelineConfig::set_dynamic_state(const std::vector<vk::DynamicState>& dy_states) {
        dynamic_state.dynamic_states = dy_states;
    }

    void PipelineConfig::enable_alpha_blend() {
        color_blend_state.color_blend_attachment_state.blendEnable = VK_TRUE;
        color_blend_state.color_blend_attachment_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_blend_state.color_blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_state.color_blend_attachment_state.colorBlendOp = vk::BlendOp::eAdd;
        color_blend_state.color_blend_attachment_state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_state.color_blend_attachment_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        color_blend_state.color_blend_attachment_state.alphaBlendOp = vk::BlendOp::eAdd;
    }

    void PipelineConfig::enable_depth_test() {
        depth_stencil_state.depth_test_enable = VK_TRUE;
        depth_stencil_state.depth_write_enable = VK_TRUE;
        depth_stencil_state.depth_compare_op = vk::CompareOp::eLess;
    }

    Pipeline::Pipeline(const std::string& name, Device* device, RenderPass* render_pass,
                       const std::shared_ptr<PipelineLayout>& layout,
                       const std::shared_ptr<Shader>& vertex_shader,
                       const std::shared_ptr<Shader>& fragment_shader,
                       const PipelineConfig& config) : m_name(name), m_device(device), m_render_pass(render_pass), m_layout(layout), m_config(config) {
        auto shader_stages = create_shader_stages(vertex_shader, fragment_shader);
        auto vertex_input_state = create_vertex_input_state();
        auto input_assembly_state = create_input_assembly_state();
        auto rasterization_state = create_rasterization_state();
        auto multisample_state = create_multisample_state();
        auto depth_stencil_state = create_depth_stencil_state();
        auto color_blend_state = create_color_blend_state();
        auto viewport_state = create_viewport_state();
        auto dynamic_state = create_dynamic_state();

        vk::GraphicsPipelineCreateInfo pipeline_create_info = {};
        pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
        pipeline_create_info.pStages = shader_stages.data();
        pipeline_create_info.pVertexInputState = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState = &multisample_state;
        pipeline_create_info.pDepthStencilState = &depth_stencil_state;
        pipeline_create_info.pColorBlendState = &color_blend_state;
        pipeline_create_info.pDynamicState = &dynamic_state;
        pipeline_create_info.layout = m_layout->get_pipeline_layout();
        pipeline_create_info.renderPass = m_render_pass->get_render_pass();
        pipeline_create_info.subpass = 0;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.basePipelineIndex = 0;

        auto result = m_device->get_device().createGraphicsPipeline(m_device->get_pipeline_cache(), pipeline_create_info);
        if(result.result != vk::Result::eSuccess) {
            LOG_FATAL("Failed to create graphics pipeline");
        }
        m_pipeline = result.value;
        LOG_INFO("Vulkan graphics pipeline created successfully");
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> Pipeline::create_shader_stages(const std::shared_ptr<Shader>& vertex_shader, const std::shared_ptr<Shader>& fragment_shader) {
        std::array<vk::PipelineShaderStageCreateInfo, 2> pipeline_shader_stage;
        vk::PipelineShaderStageCreateInfo vertex_shader_stage_info = {};
        vertex_shader_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
        vertex_shader_stage_info.module = vertex_shader->get_shader_module();
        vertex_shader_stage_info.pName = "main";
        vertex_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[0] = vertex_shader_stage_info;
        vk::PipelineShaderStageCreateInfo fragment_shader_stage_info = {};
        fragment_shader_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
        fragment_shader_stage_info.module = fragment_shader->get_shader_module();
        fragment_shader_stage_info.pName = "main";
        fragment_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[1] = fragment_shader_stage_info;
        return pipeline_shader_stage;
    }

    vk::PipelineVertexInputStateCreateInfo Pipeline::create_vertex_input_state() {
        vk::PipelineVertexInputStateCreateInfo vertex_input_state_info = {};
        vertex_input_state_info.vertexBindingDescriptionCount = static_cast<uint32_t>(m_config.vertex_input_state.vertex_bindings.size());
        vertex_input_state_info.pVertexBindingDescriptions = m_config.vertex_input_state.vertex_bindings.data();
        vertex_input_state_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_config.vertex_input_state.vertex_attributes.size());
        vertex_input_state_info.pVertexAttributeDescriptions = m_config.vertex_input_state.vertex_attributes.data();
        return vertex_input_state_info;
    }

    vk::PipelineInputAssemblyStateCreateInfo Pipeline::create_input_assembly_state() {
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_info = {};
        input_assembly_state_info.topology = m_config.input_assembly_state.topology;
        input_assembly_state_info.primitiveRestartEnable = m_config.input_assembly_state.primitive_restart_enable;
        return input_assembly_state_info;
    }

    vk::PipelineViewportStateCreateInfo Pipeline::create_viewport_state() {
        auto viewport = get_viewport(100.0f, 100.0f);
        auto scissor = get_scissor(100.0f, 100.0f);

        vk::PipelineViewportStateCreateInfo viewport_state_info = {};
        viewport_state_info.viewportCount = 1;
        viewport_state_info.pViewports = &viewport;
        viewport_state_info.scissorCount = 1;
        viewport_state_info.pScissors = &scissor;
        return viewport_state_info;
    }

    vk::PipelineDynamicStateCreateInfo Pipeline::create_dynamic_state() {
        vk::PipelineDynamicStateCreateInfo dynamic_state_info = {};
        dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(m_config.dynamic_state.dynamic_states.size());
        dynamic_state_info.pDynamicStates = m_config.dynamic_state.dynamic_states.data();
        return dynamic_state_info;
    }

    vk::PipelineRasterizationStateCreateInfo Pipeline::create_rasterization_state() {
        vk::PipelineRasterizationStateCreateInfo rasterization_state_info = {};
        rasterization_state_info.depthClampEnable = m_config.rasterization_state.depth_clamp_enable;
        rasterization_state_info.rasterizerDiscardEnable = m_config.rasterization_state.rasterizer_discard_enable;
        rasterization_state_info.polygonMode = m_config.rasterization_state.polygon_mode;
        rasterization_state_info.lineWidth = m_config.rasterization_state.line_width;
        rasterization_state_info.cullMode = m_config.rasterization_state.cull_mode;
        rasterization_state_info.frontFace = m_config.rasterization_state.front_face;
        rasterization_state_info.depthBiasEnable = m_config.rasterization_state.depth_bias_enable;
        rasterization_state_info.depthBiasConstantFactor = m_config.rasterization_state.depth_bias_constant_factor;
        rasterization_state_info.depthBiasClamp = m_config.rasterization_state.depth_bias_clamp;
        rasterization_state_info.depthBiasSlopeFactor = m_config.rasterization_state.depth_bias_slope_factor;
        return rasterization_state_info;
    }

    vk::PipelineMultisampleStateCreateInfo Pipeline::create_multisample_state() {
        vk::PipelineMultisampleStateCreateInfo multisample_state_info = {};
        multisample_state_info.rasterizationSamples = m_config.multisample_state.rasterization_samples;
        multisample_state_info.sampleShadingEnable = m_config.multisample_state.sample_shading_enable;
        multisample_state_info.minSampleShading = m_config.multisample_state.min_sample_shading;
        multisample_state_info.pSampleMask = nullptr;
        multisample_state_info.alphaToCoverageEnable = VK_FALSE;
        multisample_state_info.alphaToOneEnable = VK_FALSE;
        return multisample_state_info;
    }

    vk::PipelineDepthStencilStateCreateInfo Pipeline::create_depth_stencil_state() {
        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_info = {};
        depth_stencil_state_info.depthTestEnable = m_config.depth_stencil_state.depth_test_enable;
        depth_stencil_state_info.depthWriteEnable = m_config.depth_stencil_state.depth_write_enable;
        depth_stencil_state_info.depthCompareOp = m_config.depth_stencil_state.depth_compare_op;
        depth_stencil_state_info.depthBoundsTestEnable = m_config.depth_stencil_state.depth_bounds_test_enable;
        depth_stencil_state_info.stencilTestEnable = m_config.depth_stencil_state.stencil_test_enable;
        depth_stencil_state_info.front = vk::StencilOpState{};
        depth_stencil_state_info.back = vk::StencilOpState{};
        depth_stencil_state_info.minDepthBounds = 0.0f;
        depth_stencil_state_info.maxDepthBounds = 1.0f;
        return depth_stencil_state_info;
    }

    vk::PipelineColorBlendStateCreateInfo Pipeline::create_color_blend_state() {
        vk::PipelineColorBlendStateCreateInfo color_blend_state_info = {};
        color_blend_state_info.logicOpEnable = VK_FALSE;
        color_blend_state_info.logicOp = vk::LogicOp::eClear;
        color_blend_state_info.attachmentCount = 1;
        color_blend_state_info.pAttachments = &m_config.color_blend_state.color_blend_attachment_state;
        color_blend_state_info.blendConstants[0] = 0.0f;
        color_blend_state_info.blendConstants[1] = 0.0f;
        color_blend_state_info.blendConstants[2] = 0.0f;
        color_blend_state_info.blendConstants[3] = 0.0f;
        return color_blend_state_info;
    }

    Pipeline::~Pipeline() {
        m_device->get_device().destroyPipeline(m_pipeline);
    }
}
