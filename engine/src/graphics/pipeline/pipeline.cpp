#include "graphics/pipeline/pipeline.h"

#include <utility>
#include <stdexcept>
#include <algorithm>
#include "graphics/device.h"
#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"

namespace Comet {
    PipelineLayout::PipelineLayout(Device& device, const ShaderLayout& layout)
        : m_device(device) {
        std::vector<vk::DescriptorSetLayout> vk_set_layouts;
        vk_set_layouts.reserve(layout.descriptor_set_layouts.size());
        for(auto& set_layout : layout.descriptor_set_layouts) {
            vk_set_layouts.push_back(set_layout->get());
        }
        std::vector<vk::PushConstantRange> vk_push_constants;
        vk_push_constants.reserve(layout.push_constants.size());
        for(auto& push_constant : layout.push_constants) {
            vk_push_constants.push_back(push_constant->get());
        }

        vk::PipelineLayoutCreateInfo pipeline_layout_create_info = {};
        pipeline_layout_create_info.setLayoutCount =
            static_cast<uint32_t>(vk_set_layouts.size());
        pipeline_layout_create_info.pSetLayouts = vk_set_layouts.data();
        pipeline_layout_create_info.pushConstantRangeCount =
            static_cast<uint32_t>(vk_push_constants.size());
        pipeline_layout_create_info.pPushConstantRanges = vk_push_constants.data();

        m_pipeline_layout =
            m_device.get().createPipelineLayout(pipeline_layout_create_info);
        LOG_INFO("Vulkan pipeline layout created successfully");
    }

    PipelineLayout::~PipelineLayout() {
        m_device.get().destroyPipelineLayout(m_pipeline_layout);
    }

    Pipeline::Pipeline(std::string name, Device& device, RenderPass& render_pass,
        const std::shared_ptr<PipelineLayout>& layout,
        const std::shared_ptr<Shader>& vertex_shader,
        const std::shared_ptr<Shader>& fragment_shader, const PipelineConfig& config)
        : m_name(std::move(name)), m_device(device), m_layout(layout) {
        auto shader_stages = create_shader_stages(vertex_shader, fragment_shader);
        auto vertex_input_state = create_vertex_input_state(config);
        auto input_assembly_state = create_input_assembly_state(config);
        auto rasterization_state = create_rasterization_state(config);
        auto multisample_state = create_multisample_state(config);
        auto depth_stencil_state = create_depth_stencil_state(config);
        auto color_blend_state = create_color_blend_state(config);
        auto viewport_state = create_viewport_state(config.viewport, config.scissor);
        auto dynamic_state = create_dynamic_state(config);

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
        pipeline_create_info.renderPass = render_pass.get();
        pipeline_create_info.subpass = config.subpass;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.basePipelineIndex = 0;

        auto result = m_device.get().createGraphicsPipeline(
            m_device.get_pipeline_cache(), pipeline_create_info);
        if(result.result != vk::Result::eSuccess) {
            LOG_FATAL("Failed to create graphics pipeline");
        }
        m_pipeline = result.value;
        LOG_INFO("Vulkan graphics pipeline created successfully");
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> Pipeline::create_shader_stages(
        const std::shared_ptr<Shader>& vertex_shader,
        const std::shared_ptr<Shader>& fragment_shader) {
        std::array<vk::PipelineShaderStageCreateInfo, 2> pipeline_shader_stage;
        vk::PipelineShaderStageCreateInfo vertex_shader_stage_info = {};
        vertex_shader_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
        vertex_shader_stage_info.module = vertex_shader->get();
        vertex_shader_stage_info.pName =
            vertex_shader->get_interface().get_entry_point().c_str();
        vertex_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[0] = vertex_shader_stage_info;
        vk::PipelineShaderStageCreateInfo fragment_shader_stage_info = {};
        fragment_shader_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
        fragment_shader_stage_info.module = fragment_shader->get();
        fragment_shader_stage_info.pName =
            fragment_shader->get_interface().get_entry_point().c_str();
        fragment_shader_stage_info.pSpecializationInfo = nullptr;
        pipeline_shader_stage[1] = fragment_shader_stage_info;
        return pipeline_shader_stage;
    }

    vk::PipelineVertexInputStateCreateInfo Pipeline::create_vertex_input_state(
        const PipelineConfig& config) {
        vk::PipelineVertexInputStateCreateInfo vertex_input_state_info = {};
        vertex_input_state_info.vertexBindingDescriptionCount =
            static_cast<uint32_t>(config.vertex_input_state.vertex_bindings.size());
        vertex_input_state_info.pVertexBindingDescriptions =
            config.vertex_input_state.vertex_bindings.data();
        vertex_input_state_info.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(config.vertex_input_state.vertex_attributes.size());
        vertex_input_state_info.pVertexAttributeDescriptions =
            config.vertex_input_state.vertex_attributes.data();
        return vertex_input_state_info;
    }

    vk::PipelineInputAssemblyStateCreateInfo Pipeline::create_input_assembly_state(
        const PipelineConfig& config) {
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_info = {};
        input_assembly_state_info.topology =
            Graphics::primitive_topology_to_vk(config.input_assembly_state.topology);
        input_assembly_state_info.primitiveRestartEnable =
            config.input_assembly_state.primitive_restart_enable;
        return input_assembly_state_info;
    }

    vk::PipelineViewportStateCreateInfo Pipeline::create_viewport_state(
        const vk::Viewport& viewport, const vk::Rect2D& scissor) {
        vk::PipelineViewportStateCreateInfo viewport_state_info = {};
        viewport_state_info.viewportCount = 1;
        viewport_state_info.pViewports = &viewport;
        viewport_state_info.scissorCount = 1;
        viewport_state_info.pScissors = &scissor;
        return viewport_state_info;
    }

    vk::PipelineDynamicStateCreateInfo Pipeline::create_dynamic_state(
        const PipelineConfig& config) {
        vk::PipelineDynamicStateCreateInfo dynamic_state_info = {};
        dynamic_state_info.dynamicStateCount =
            static_cast<uint32_t>(config.dynamic_state.dynamic_states.size());
        dynamic_state_info.pDynamicStates = config.dynamic_state.dynamic_states.data();
        return dynamic_state_info;
    }

    vk::PipelineRasterizationStateCreateInfo Pipeline::create_rasterization_state(
        const PipelineConfig& config) {
        vk::PipelineRasterizationStateCreateInfo rasterization_state_info = {};
        rasterization_state_info.depthClampEnable =
            config.rasterization_state.depth_clamp_enable;
        rasterization_state_info.rasterizerDiscardEnable =
            config.rasterization_state.rasterizer_discard_enable;
        rasterization_state_info.polygonMode =
            Graphics::polygon_mode_to_vk(config.rasterization_state.polygon_mode);
        rasterization_state_info.lineWidth = config.rasterization_state.line_width;
        rasterization_state_info.cullMode = Graphics::cull_mode_to_vk(
            Flags<CullMode>(config.rasterization_state.cull_mode));
        rasterization_state_info.frontFace =
            Graphics::front_face_to_vk(config.rasterization_state.front_face);
        rasterization_state_info.depthBiasEnable =
            config.rasterization_state.depth_bias_enable;
        rasterization_state_info.depthBiasConstantFactor =
            config.rasterization_state.depth_bias_constant_factor;
        rasterization_state_info.depthBiasClamp =
            config.rasterization_state.depth_bias_clamp;
        rasterization_state_info.depthBiasSlopeFactor =
            config.rasterization_state.depth_bias_slope_factor;
        return rasterization_state_info;
    }

    vk::PipelineMultisampleStateCreateInfo Pipeline::create_multisample_state(
        const PipelineConfig& config) {
        vk::PipelineMultisampleStateCreateInfo multisample_state_info = {};
        multisample_state_info.rasterizationSamples =
            Graphics::sample_count_to_vk(config.multisample_state.rasterization_samples);
        multisample_state_info.sampleShadingEnable =
            config.multisample_state.sample_shading_enable;
        multisample_state_info.minSampleShading =
            config.multisample_state.min_sample_shading;
        multisample_state_info.pSampleMask = nullptr;
        multisample_state_info.alphaToCoverageEnable = VK_FALSE;
        multisample_state_info.alphaToOneEnable = VK_FALSE;
        return multisample_state_info;
    }

    vk::PipelineDepthStencilStateCreateInfo Pipeline::create_depth_stencil_state(
        const PipelineConfig& config) {
        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_info = {};
        depth_stencil_state_info.depthTestEnable =
            config.depth_stencil_state.depth_test_enable;
        depth_stencil_state_info.depthWriteEnable =
            config.depth_stencil_state.depth_write_enable;
        depth_stencil_state_info.depthCompareOp =
            Graphics::compare_op_to_vk(config.depth_stencil_state.depth_compare_op);
        depth_stencil_state_info.depthBoundsTestEnable =
            config.depth_stencil_state.depth_bounds_test_enable;
        depth_stencil_state_info.stencilTestEnable =
            config.depth_stencil_state.stencil_test_enable;
        depth_stencil_state_info.front = vk::StencilOpState{};
        depth_stencil_state_info.back = vk::StencilOpState{};
        depth_stencil_state_info.minDepthBounds = 0.0f;
        depth_stencil_state_info.maxDepthBounds = 1.0f;
        return depth_stencil_state_info;
    }

    vk::PipelineColorBlendStateCreateInfo Pipeline::create_color_blend_state(
        const PipelineConfig& config) {
        vk::PipelineColorBlendStateCreateInfo color_blend_state_info = {};
        color_blend_state_info.logicOpEnable = VK_FALSE;
        color_blend_state_info.logicOp = vk::LogicOp::eClear;
        color_blend_state_info.attachmentCount = 1;
        color_blend_state_info.pAttachments = &config.color_blend_state;
        color_blend_state_info.blendConstants[0] = 0.0f;
        color_blend_state_info.blendConstants[1] = 0.0f;
        color_blend_state_info.blendConstants[2] = 0.0f;
        color_blend_state_info.blendConstants[3] = 0.0f;
        return color_blend_state_info;
    }

    Pipeline::~Pipeline() {
        m_device.get().destroyPipeline(m_pipeline);
    }

    PipelineManager::PipelineManager(Device& device, RenderPass& render_pass)
        : m_device(device), m_render_pass(render_pass) {
        LOG_INFO("PipelineManager created");
    }

    std::shared_ptr<Pipeline> PipelineManager::create_pipeline(const std::string& name,
        const ShaderLayout& layout, const PipelineConfig& config,
        const std::shared_ptr<Shader>& vert_shader,
        const std::shared_ptr<Shader>& frag_shader) {
        if(!vert_shader || !frag_shader
            || vert_shader->get_interface().get_stage() != ShaderStage::Vertex
            || frag_shader->get_interface().get_stage() != ShaderStage::Fragment) {
            throw std::invalid_argument(
                "Graphics pipeline requires vertex/fragment shaders");
        }
        layout.validate(vert_shader->get_interface());
        layout.validate(frag_shader->get_interface());
        PipelineKey key(layout, config, *vert_shader, *frag_shader, m_render_pass);
        collect_unused();
        const auto it = m_pipelines.find(key);
        if(it != m_pipelines.end()) {
            if(auto pipeline = it->second.lock()) {
                LOG_DEBUG("Pipeline '{}' reuses compatible cached state", name);
                return pipeline;
            }
        }

        auto pipeline_layout = std::make_shared<PipelineLayout>(m_device, layout);

        auto pipeline = std::make_shared<Pipeline>(name, m_device, m_render_pass,
            pipeline_layout, vert_shader, frag_shader, key.config);

        m_pipelines.insert_or_assign(std::move(key), pipeline);

        LOG_INFO("Pipeline '{}' created successfully", name);
        return pipeline;
    }

    void PipelineManager::collect_unused() {
        std::erase_if(
            m_pipelines, [](const auto& entry) { return entry.second.expired(); });
    }

}
