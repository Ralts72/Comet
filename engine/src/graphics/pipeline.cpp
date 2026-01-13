#include "pipeline.h"
#include "device.h"
#include "shader.h"
#include "render_pass.h"

namespace Comet {
    PipelineLayout::PipelineLayout(Device* device, const ShaderLayout& layout) : m_device(device) {
        std::vector<vk::DescriptorSetLayout> vk_set_layouts;
        vk_set_layouts.reserve(layout.descriptor_set_layouts.size());
        for(auto & set_layout : layout.descriptor_set_layouts) {
            vk_set_layouts.push_back(set_layout->get());
        }
        std::vector<vk::PushConstantRange> vk_push_constants;
        vk_push_constants.reserve(layout.push_constants.size());
        for(auto & push_constant : layout.push_constants) {
            vk_push_constants.push_back(push_constant->get());
        }

        vk::PipelineLayoutCreateInfo pipeline_layout_create_info = {};
        pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(vk_set_layouts.size());
        pipeline_layout_create_info.pSetLayouts = vk_set_layouts.data();
        pipeline_layout_create_info.pushConstantRangeCount = static_cast<uint32_t>(vk_push_constants.size());
        pipeline_layout_create_info.pPushConstantRanges = vk_push_constants.data();

        m_pipeline_layout = m_device->get().createPipelineLayout(pipeline_layout_create_info);
        LOG_INFO("Vulkan pipeline layout created successfully");
    }

    PipelineLayout::~PipelineLayout() {
        m_device->get().destroyPipelineLayout(m_pipeline_layout);
    }

    void PipelineConfig::set_vertex_input_state(const VertexInputDescription& description) {
        vertex_input_state.vertex_bindings = description.get_bindings();
        vertex_input_state.vertex_attributes = description.get_attributes();
    }

    void PipelineConfig::set_input_assembly_state(const Topology topology, const bool primitive_restart_enable) {
        input_assembly_state.topology = topology;
        input_assembly_state.primitive_restart_enable = primitive_restart_enable;
    }

    void PipelineConfig::set_rasterization_state(const PipelineRasterizationState& raster_state) {
        rasterization_state = raster_state;
    }

    void PipelineConfig::set_multisample_state(const SampleCount samples, const bool sample_shading_enable,
        const float min_sample_shading) {
        multisample_state.rasterization_samples = samples;
        multisample_state.sample_shading_enable = sample_shading_enable;
        multisample_state.min_sample_shading = min_sample_shading;
    }

    void PipelineConfig::set_depth_stencil_state(const PipelineDepthStencilState& ds_state) {
        depth_stencil_state = ds_state;
    }

    void PipelineConfig::set_color_blend_attachment_state(const PipelineColorBlendState& cb_state) {
        color_blend_state.blendEnable = cb_state.blend_enable;
        color_blend_state.srcColorBlendFactor = Graphics::blend_factor_to_vk(cb_state.src_color_blend_factor);
        color_blend_state.dstColorBlendFactor = Graphics::blend_factor_to_vk(cb_state.dst_color_blend_factor);
        color_blend_state.colorBlendOp = Graphics::blend_op_to_vk(cb_state.color_blend_op);
        color_blend_state.srcAlphaBlendFactor = Graphics::blend_factor_to_vk(cb_state.src_alpha_blend_factor);
        color_blend_state.dstAlphaBlendFactor = Graphics::blend_factor_to_vk(cb_state.dst_alpha_blend_factor);
        color_blend_state.alphaBlendOp = Graphics::blend_op_to_vk(cb_state.alpha_blend_op);
    }

    void PipelineConfig::set_dynamic_state(const std::vector<DynamicState>& dy_states) {
        std::vector<vk::DynamicState> dynamic_states;
        dynamic_states.reserve(dy_states.size());
        for(const auto& dy_state : dy_states) {
            dynamic_states.push_back(Graphics::dynamic_state_to_vk(dy_state));
        }
        dynamic_state.dynamic_states = dynamic_states;
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
        pipeline_create_info.layout = m_layout->get();
        pipeline_create_info.renderPass = m_render_pass->get();
        pipeline_create_info.subpass = 0;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.basePipelineIndex = 0;

        auto result = m_device->get().createGraphicsPipeline(m_device->get_pipeline_cache(), pipeline_create_info);
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
        vertex_shader_stage_info.module = vertex_shader->get();
        vertex_shader_stage_info.pName = "main";
        vertex_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[0] = vertex_shader_stage_info;
        vk::PipelineShaderStageCreateInfo fragment_shader_stage_info = {};
        fragment_shader_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
        fragment_shader_stage_info.module = fragment_shader->get();
        fragment_shader_stage_info.pName = "main";
        fragment_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[1] = fragment_shader_stage_info;
        return pipeline_shader_stage;
    }

    vk::PipelineVertexInputStateCreateInfo Pipeline::create_vertex_input_state() const {
        vk::PipelineVertexInputStateCreateInfo vertex_input_state_info = {};
        vertex_input_state_info.vertexBindingDescriptionCount = static_cast<uint32_t>(m_config.vertex_input_state.vertex_bindings.size());
        vertex_input_state_info.pVertexBindingDescriptions = m_config.vertex_input_state.vertex_bindings.data();
        vertex_input_state_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_config.vertex_input_state.vertex_attributes.size());
        vertex_input_state_info.pVertexAttributeDescriptions = m_config.vertex_input_state.vertex_attributes.data();
        return vertex_input_state_info;
    }

    vk::PipelineInputAssemblyStateCreateInfo Pipeline::create_input_assembly_state() const {
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_info = {};
        input_assembly_state_info.topology = Graphics::primitive_topology_to_vk(m_config.input_assembly_state.topology);
        input_assembly_state_info.primitiveRestartEnable = m_config.input_assembly_state.primitive_restart_enable;
        return input_assembly_state_info;
    }

    vk::PipelineViewportStateCreateInfo Pipeline::create_viewport_state() {
        m_config.viewport = Graphics::get_viewport(100.0f, 100.0f);
        m_config.scissor = Graphics::get_scissor(100.0f, 100.0f);
        vk::PipelineViewportStateCreateInfo viewport_state_info = {};
        viewport_state_info.viewportCount = 1;
        viewport_state_info.pViewports = &m_config.viewport;
        viewport_state_info.scissorCount = 1;
        viewport_state_info.pScissors = &m_config.scissor;
        return viewport_state_info;
    }

    vk::PipelineDynamicStateCreateInfo Pipeline::create_dynamic_state() const {
        vk::PipelineDynamicStateCreateInfo dynamic_state_info = {};
        dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(m_config.dynamic_state.dynamic_states.size());
        dynamic_state_info.pDynamicStates = m_config.dynamic_state.dynamic_states.data();
        return dynamic_state_info;
    }

    vk::PipelineRasterizationStateCreateInfo Pipeline::create_rasterization_state() const {
        vk::PipelineRasterizationStateCreateInfo rasterization_state_info = {};
        rasterization_state_info.depthClampEnable = m_config.rasterization_state.depth_clamp_enable;
        rasterization_state_info.rasterizerDiscardEnable = m_config.rasterization_state.rasterizer_discard_enable;
        rasterization_state_info.polygonMode = Graphics::polygon_mode_to_vk(m_config.rasterization_state.polygon_mode);
        rasterization_state_info.lineWidth = m_config.rasterization_state.line_width;
        rasterization_state_info.cullMode = Graphics::cull_mode_to_vk(Flags<CullMode>(m_config.rasterization_state.cull_mode));
        rasterization_state_info.frontFace = Graphics::front_face_to_vk(m_config.rasterization_state.front_face);
        rasterization_state_info.depthBiasEnable = m_config.rasterization_state.depth_bias_enable;
        rasterization_state_info.depthBiasConstantFactor = m_config.rasterization_state.depth_bias_constant_factor;
        rasterization_state_info.depthBiasClamp = m_config.rasterization_state.depth_bias_clamp;
        rasterization_state_info.depthBiasSlopeFactor = m_config.rasterization_state.depth_bias_slope_factor;
        return rasterization_state_info;
    }

    vk::PipelineMultisampleStateCreateInfo Pipeline::create_multisample_state() const {
        vk::PipelineMultisampleStateCreateInfo multisample_state_info = {};
        multisample_state_info.rasterizationSamples = Graphics::sample_count_to_vk(m_config.multisample_state.rasterization_samples);
        multisample_state_info.sampleShadingEnable = m_config.multisample_state.sample_shading_enable;
        multisample_state_info.minSampleShading = m_config.multisample_state.min_sample_shading;
        multisample_state_info.pSampleMask = nullptr;
        multisample_state_info.alphaToCoverageEnable = VK_FALSE;
        multisample_state_info.alphaToOneEnable = VK_FALSE;
        return multisample_state_info;
    }

    vk::PipelineDepthStencilStateCreateInfo Pipeline::create_depth_stencil_state() const {
        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_info = {};
        depth_stencil_state_info.depthTestEnable = m_config.depth_stencil_state.depth_test_enable;
        depth_stencil_state_info.depthWriteEnable = m_config.depth_stencil_state.depth_write_enable;
        depth_stencil_state_info.depthCompareOp = Graphics::compare_op_to_vk(m_config.depth_stencil_state.depth_compare_op);
        depth_stencil_state_info.depthBoundsTestEnable = m_config.depth_stencil_state.depth_bounds_test_enable;
        depth_stencil_state_info.stencilTestEnable = m_config.depth_stencil_state.stencil_test_enable;
        depth_stencil_state_info.front = vk::StencilOpState{};
        depth_stencil_state_info.back = vk::StencilOpState{};
        depth_stencil_state_info.minDepthBounds = 0.0f;
        depth_stencil_state_info.maxDepthBounds = 1.0f;
        return depth_stencil_state_info;
    }

    vk::PipelineColorBlendStateCreateInfo Pipeline::create_color_blend_state() const {
        vk::PipelineColorBlendStateCreateInfo color_blend_state_info = {};
        color_blend_state_info.logicOpEnable = VK_FALSE;
        color_blend_state_info.logicOp = vk::LogicOp::eClear;
        color_blend_state_info.attachmentCount = 1;
        color_blend_state_info.pAttachments = &m_config.color_blend_state;
        color_blend_state_info.blendConstants[0] = 0.0f;
        color_blend_state_info.blendConstants[1] = 0.0f;
        color_blend_state_info.blendConstants[2] = 0.0f;
        color_blend_state_info.blendConstants[3] = 0.0f;
        return color_blend_state_info;
    }

    Pipeline::~Pipeline() {
        m_device->get().destroyPipeline(m_pipeline);
    }

    PipelineManager::PipelineManager(Device* device, RenderPass* render_pass)
        : m_device(device), m_render_pass(render_pass) {
        LOG_INFO("PipelineManager created");
    }

    std::shared_ptr<Pipeline> PipelineManager::create_pipeline(
        const std::string& name,
        const ShaderLayout& layout,
        const VertexInputDescription& vertex_input,
        const PipelineConfig& config,
        std::shared_ptr<Shader> vert_shader,
        std::shared_ptr<Shader> frag_shader) {

        auto it = m_pipelines.find(name);
        if (it != m_pipelines.end()) {
            LOG_DEBUG("Pipeline '{}' already exists, returning cached version", name);
            return it->second;
        }

        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device, layout);

        auto pipeline = std::make_shared<Pipeline>(
            name, m_device, m_render_pass,
            pipeline_layout,
            vert_shader, frag_shader,
            config
        );

        m_pipelines[name] = pipeline;

        LOG_INFO("Pipeline '{}' created successfully", name);
        return pipeline;
    }

    std::shared_ptr<Pipeline> PipelineManager::get_pipeline(const std::string& name) const {
        auto it = m_pipelines.find(name);
        if (it != m_pipelines.end()) {
            return it->second;
        }
        LOG_WARN("Pipeline '{}' not found", name);
        return nullptr;
    }
}
