#include "graphics/pipeline/shader_interface.h"
#include "graphics/pipeline/shader.h"
#include "core/engine.h"
#include "support/engine_fixture.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/pipeline_key.h"
#include "graphics/pipeline/vertex_description.h"
#include <algorithm>
#include <array>
#include "render/material.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"
#include "debug_line_vert.h"
#include "debug_line_frag.h"
#include "interface_array_vert.h"
#include "material_integer_frag.h"
#include "runtime_array_frag.h"
#include "pipeline_triangle_vert.h"
#include "pipeline_color_frag.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/command/command_buffer.h"
#include "graphics/queue.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "graphics/context.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>

namespace Comet::Tests {
    TEST(ShaderInterfaceTest, ReflectsProductionStagesBindingsAndPushConstants) {
        const ShaderInterface vertex(MATERIAL_MESH_VERT);
        EXPECT_EQ(vertex.get_entry_point(), "main");
        EXPECT_EQ(vertex.get_stage(), ShaderStage::Vertex);
        ASSERT_EQ(vertex.get_bindings().size(), 1u);
        const auto& frame = vertex.get_bindings().front();
        EXPECT_EQ(frame.set, 0u);
        EXPECT_EQ(frame.binding, 0u);
        EXPECT_EQ(frame.count, 1u);
        EXPECT_EQ(frame.block_size, 128u);
        EXPECT_EQ(frame.type, DescriptorType::UniformBuffer);
        ASSERT_EQ(frame.members.size(), 2u);
        EXPECT_EQ(frame.members[0].format, Format::UNDEFINED);
        EXPECT_EQ(frame.members[1].format, Format::UNDEFINED);
        ASSERT_EQ(vertex.get_push_constants().size(), 1u);
        EXPECT_TRUE(vertex.get_push_constants()[0].stages == ShaderStage::Vertex);
        EXPECT_EQ(vertex.get_push_constants()[0].offset, 0u);
        EXPECT_EQ(vertex.get_push_constants()[0].size, 64u);

        const ShaderInterface fragment(MATERIAL_TEXTURED_FRAG);
        EXPECT_EQ(fragment.get_stage(), ShaderStage::Fragment);
        ASSERT_EQ(fragment.get_bindings().size(), 3u);
        EXPECT_TRUE(fragment.get_push_constants().empty());
        const auto& parameters = fragment.get_bindings()[0];
        EXPECT_EQ(parameters.set, 1u);
        EXPECT_EQ(parameters.binding, 0u);
        EXPECT_EQ(parameters.block_size, 32u);
        ASSERT_EQ(parameters.members.size(), 2u);
        EXPECT_EQ(parameters.members[0].name, "tint");
        EXPECT_EQ(parameters.members[0].offset, 0u);
        EXPECT_EQ(parameters.members[0].format, Format::R32G32B32A32_SFLOAT);
        EXPECT_EQ(parameters.members[1].name, "blend");
        EXPECT_EQ(parameters.members[1].offset, 16u);
        EXPECT_EQ(parameters.members[1].format, Format::R32_SFLOAT);
        for(uint32_t index = 1; index <= 2; ++index) {
            EXPECT_EQ(fragment.get_bindings()[index].binding, index);
            EXPECT_EQ(fragment.get_bindings()[index].type,
                DescriptorType::CombinedImageSampler);
        }
        EXPECT_TRUE(ShaderInterface(DEBUG_LINE_VERT).get_bindings().empty());
        EXPECT_EQ(ShaderInterface(DEBUG_LINE_VERT).get_push_constants().size(), 1u);
        EXPECT_TRUE(ShaderInterface(DEBUG_LINE_FRAG).get_bindings().empty());
    }

    TEST(ShaderInterfaceTest, OwnsReflectedValuesAfterInputAndParserAreGone) {
        const auto reflect = [] {
            auto temporary = std::vector<uint32_t>(
                MATERIAL_TEXTURED_FRAG.begin(), MATERIAL_TEXTURED_FRAG.end());
            return ShaderInterface(temporary);
        };
        const auto interface = reflect();
        EXPECT_EQ(interface.get_bindings()[0].members[0].name, "tint");
        EXPECT_NO_THROW(
            MaterialLayout::find_builtin("unlit_texture_blend")->validate(interface));
    }

    TEST(ShaderInterfaceTest, RejectsMalformedCodeMissingEntryAndRuntimeArrays) {
        EXPECT_THROW(ShaderInterface(std::span<const uint32_t>{}), std::invalid_argument);
        EXPECT_THROW(ShaderInterface(std::array<uint32_t, 5>{}), std::invalid_argument);
        EXPECT_THROW(
            ShaderInterface(MATERIAL_MESH_VERT, "missing"), std::invalid_argument);
        EXPECT_THROW(ShaderInterface(MATERIAL_MESH_VERT, ""), std::invalid_argument);
        EXPECT_THROW(ShaderInterface(MATERIAL_MESH_VERT, std::string("main\0other", 10)),
            std::invalid_argument);
        EXPECT_THROW(ShaderInterface{RUNTIME_ARRAY_FRAG}, std::invalid_argument);
        auto truncated = std::span(MATERIAL_MESH_VERT).first(6);
        EXPECT_THROW(ShaderInterface{truncated}, std::invalid_argument);
        auto invalid_instruction =
            std::vector<uint32_t>(MATERIAL_MESH_VERT.begin(), MATERIAL_MESH_VERT.end());
        invalid_instruction[5] = 0;
        EXPECT_THROW(ShaderInterface{invalid_instruction}, std::invalid_argument);
    }

    TEST(ShaderInterfaceTest, ChecksMaterialBlockOffsetsFormatsAndTextureBindings) {
        const ShaderInterface textured(MATERIAL_TEXTURED_FRAG);
        const ShaderInterface solid(MATERIAL_SOLID_FRAG);
        EXPECT_NO_THROW(
            MaterialLayout::find_builtin("unlit_texture_blend")->validate(textured));
        EXPECT_NO_THROW(MaterialLayout::find_builtin("unlit_color")->validate(solid));
        EXPECT_THROW(MaterialLayout::find_builtin("unlit_texture_blend")->validate(solid),
            std::invalid_argument);
        EXPECT_THROW(MaterialLayout::find_builtin("unlit_color")
                         ->validate(ShaderInterface(MATERIAL_INTEGER_FRAG)),
            std::invalid_argument);
        const MaterialLayout wrong_size(
            "size", {}, 48, {{"intensity", 16, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_size.validate(solid), std::invalid_argument);
        const MaterialLayout wrong_offset(
            "offset", {}, 32, {{"intensity", 20, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_offset.validate(solid), std::invalid_argument);
        const MaterialLayout missing_member(
            "member", {}, 32, {}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(missing_member.validate(solid), std::invalid_argument);
        const MaterialLayout wrong_binding("binding", {{"a", 1}, {"b", 3}}, 32,
            {{"blend", 16, 1}}, {{"tint", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_binding.validate(textured), std::invalid_argument);
    }

    using ShaderPipelineTest = EngineTest;

    TEST_F(ShaderPipelineTest, ValidatesArrayCountTypeVisibilityAndNonzeroPushOffset) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const ShaderInterface shader(INTERFACE_ARRAY_VERT);
        ASSERT_EQ(shader.get_bindings().size(), 1u);
        EXPECT_EQ(shader.get_bindings()[0].count, 4u);
        EXPECT_EQ(shader.get_bindings()[0].set, 3u);
        EXPECT_TRUE(shader.get_bindings()[0].stages == ShaderStage::Vertex);
        ASSERT_EQ(shader.get_push_constants().size(), 1u);
        EXPECT_TRUE(shader.get_push_constants()[0].stages == ShaderStage::Vertex);
        EXPECT_EQ(shader.get_push_constants()[0].offset, 16u);
        EXPECT_EQ(shader.get_push_constants()[0].size, 20u);

        auto empty =
            std::make_shared<DescriptorSetLayout>(device, DescriptorSetLayoutBindings{});
        ShaderLayout layout;
        layout.descriptor_set_layouts = {empty, empty, empty, empty};
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 16, 20)};
        const auto set_binding = [&](DescriptorType type, uint32_t count,
                                     ShaderStage stage) {
            DescriptorSetLayoutBindings bindings;
            bindings.add_binding(4, type, Flags<ShaderStage>(stage), count);
            layout.descriptor_set_layouts[3] =
                std::make_shared<DescriptorSetLayout>(device, bindings);
        };
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Vertex);
        EXPECT_NO_THROW(layout.validate(shader));
        set_binding(DescriptorType::CombinedImageSampler, 3, ShaderStage::Vertex);
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        set_binding(DescriptorType::CombinedImageSampler, 8, ShaderStage::Vertex);
        EXPECT_NO_THROW(layout.validate(shader));
        set_binding(DescriptorType::SampledImage, 4, ShaderStage::Vertex);
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Fragment);
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Vertex);

        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 16, 12)};
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 16, 20)};
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 20, 20)};
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 36)};
        EXPECT_NO_THROW(layout.validate(shader));
        layout.push_constants.clear();
        EXPECT_THROW(layout.validate(shader), std::invalid_argument);
    }

    TEST_F(ShaderPipelineTest, AcceptsDynamicBufferLayoutsAndUnusedBindings) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        const ShaderInterface shader(MATERIAL_MESH_VERT);
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBufferDynamic,
            Flags<ShaderStage>(ShaderStage::Vertex) | ShaderStage::Fragment);
        bindings.add_binding(
            7, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
        ShaderLayout layout;
        layout.descriptor_set_layouts = {
            std::make_shared<DescriptorSetLayout>(device, bindings)};
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 64)};
        EXPECT_NO_THROW(layout.validate(shader));
    }

    TEST_F(ShaderPipelineTest, RejectsIncompatibleLayoutsBeforeVulkanCreationOrCacheHit) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        RenderPass pass(device,
            {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}}, Format::R8G8B8A8_UNORM);
        PipelineManager pipelines(device, pass);
        auto vertex = std::make_shared<Shader>(device, "line", DEBUG_LINE_VERT);
        auto fragment = std::make_shared<Shader>(device, "line", DEBUG_LINE_FRAG);
        ShaderLayout layout;
        PipelineConfig config;
        EXPECT_THROW(pipelines.create_pipeline("line", layout, config, vertex, fragment),
            std::invalid_argument);
        EXPECT_THROW(pipelines.create_pipeline("line", layout, config, fragment, vertex),
            std::invalid_argument);
        EXPECT_THROW(pipelines.create_pipeline("line", layout, config, nullptr, fragment),
            std::invalid_argument);
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 64));
        config.vertex_input_state.vertex_bindings = {
            {0, 28, vk::VertexInputRate::eVertex}};
        config.vertex_input_state.vertex_attributes = {
            {0, 0, vk::Format::eR32G32B32Sfloat, 0},
            {1, 0, vk::Format::eR32G32B32A32Sfloat, 12}};
        config.input_assembly_state.topology = Topology::LineList;
        config.dynamic_state.dynamic_states = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        auto valid = pipelines.create_pipeline("line", layout, config, vertex, fragment);
        ASSERT_TRUE(valid);
        layout.push_constants.clear();
        EXPECT_THROW(pipelines.create_pipeline("line", layout, config, vertex, fragment),
            std::invalid_argument);
        EXPECT_THROW(
            Shader(device, "bad", std::span<const uint32_t>{}), std::invalid_argument);

        const ShaderInterface material(MATERIAL_TEXTURED_FRAG);
        EXPECT_THROW(layout.validate(material), std::invalid_argument);
        DescriptorSetLayoutBindings empty;
        auto empty_set = std::make_shared<DescriptorSetLayout>(device, empty);
        layout.descriptor_set_layouts = {empty_set, empty_set};
        EXPECT_THROW(layout.validate(material), std::invalid_argument);
        layout.descriptor_set_layouts = {nullptr};
        EXPECT_THROW(
            layout.validate(ShaderInterface(DEBUG_LINE_FRAG)), std::invalid_argument);
        layout.descriptor_set_layouts.clear();
        layout.push_constants = {nullptr};
        EXPECT_THROW(
            layout.validate(ShaderInterface(DEBUG_LINE_FRAG)), std::invalid_argument);
    }

    TEST_F(ShaderPipelineTest, UsesContentAndStateInsteadOfShaderAndPipelineLabels) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderPass pass(device);
        PipelineManager pipelines(device, pass);
        ShaderManager shaders(device);
        auto vertex = shaders.load_shader("vertex", PIPELINE_TRIANGLE_VERT);
        auto fragment = shaders.load_shader("fragment", PIPELINE_COLOR_FRAG);
        ShaderLayout layout;
        PipelineConfig config;
        auto original =
            pipelines.create_pipeline("same", layout, config, vertex, fragment);
        EXPECT_EQ(
            original, pipelines.create_pipeline("other", layout, config, vertex,
                          shaders.load_shader("other_fragment", PIPELINE_COLOR_FRAG)));
        config.rasterization_state.front_face = FrontFace::CCW;
        auto changed =
            pipelines.create_pipeline("same", layout, config, vertex, fragment);
        EXPECT_NE(original, changed);
        config.viewport.width = 80;
        EXPECT_NE(
            changed, pipelines.create_pipeline("same", layout, config, vertex, fragment));
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 3u);
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 2u);

        PipelineColorBlendState blend;
        blend.color_write_mask = Flags<ColorWriteMask>(ColorWriteMask::Red);
        config = PipelineConfig{};
        config.set_color_blend_attachment_state(blend);
        EXPECT_EQ(
            config.color_blend_state.colorWriteMask, vk::ColorComponentFlagBits::eR);
        EXPECT_NE(original,
            pipelines.create_pipeline("same", layout, config, vertex, fragment));

        EXPECT_EQ(fragment, shaders.load_shader("fragment", PIPELINE_COLOR_FRAG));
        auto new_fragment = shaders.load_shader("fragment", DEBUG_LINE_FRAG);
        EXPECT_NE(fragment, new_fragment);
        EXPECT_EQ(fragment->get_code(), std::vector<uint32_t>(PIPELINE_COLOR_FRAG.begin(),
                                            PIPELINE_COLOR_FRAG.end()));
        EXPECT_THROW(shaders.load_shader("fragment", std::span<const uint32_t>{}),
            std::invalid_argument);
        EXPECT_EQ(new_fragment, shaders.load_shader("fragment", DEBUG_LINE_FRAG));
    }

    TEST_F(ShaderPipelineTest, CanonicalizesLayoutsAndDynamicViewportState) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderPass pass(device);
        auto vertex = std::make_shared<Shader>(device, "vertex", PIPELINE_TRIANGLE_VERT);
        auto fragment = std::make_shared<Shader>(device, "fragment", PIPELINE_COLOR_FRAG);
        const auto make_layout = [&](bool reverse, uint32_t count) {
            DescriptorSetLayoutBindings bindings;
            if(reverse) {
                bindings.add_binding(2, DescriptorType::UniformBuffer,
                    Flags<ShaderStage>(ShaderStage::Vertex), count);
                bindings.add_binding(0, DescriptorType::Sampler,
                    Flags<ShaderStage>(ShaderStage::Fragment));
            } else {
                bindings.add_binding(0, DescriptorType::Sampler,
                    Flags<ShaderStage>(ShaderStage::Fragment));
                bindings.add_binding(2, DescriptorType::UniformBuffer,
                    Flags<ShaderStage>(ShaderStage::Vertex), count);
            }
            ShaderLayout layout;
            layout.descriptor_set_layouts.push_back(
                std::make_shared<DescriptorSetLayout>(device, bindings));
            layout.push_constants = {
                std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 16),
                std::make_shared<PushConstantRange>(ShaderStage::Fragment, 16, 16)};
            if(reverse)
                std::ranges::reverse(layout.push_constants);
            return layout;
        };
        auto a = make_layout(false, 1);
        auto b = make_layout(true, 1);
        PipelineConfig first;
        VertexInputDescription input;
        input.add_binding(1, 8, VertexInputRate::Vertex);
        input.add_binding(0, 8, VertexInputRate::Vertex);
        input.add_attribute(6, 1, Format::R32_SFLOAT, 0);
        input.add_attribute(5, 0, Format::R32_SFLOAT, 0);
        first.set_vertex_input_state(input);
        first.dynamic_state.dynamic_states = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        PipelineConfig second = first;
        std::ranges::reverse(second.vertex_input_state.vertex_bindings);
        std::ranges::reverse(second.vertex_input_state.vertex_attributes);
        second.dynamic_state.dynamic_states = {vk::DynamicState::eScissor,
            vk::DynamicState::eViewport, vk::DynamicState::eViewport};
        second.viewport.width = 800;
        second.scissor.extent = vk::Extent2D(800, 600);
        const PipelineKey ka(a, first, *vertex, *fragment, pass);
        const PipelineKey kb(b, second, *vertex, *fragment, pass);
        EXPECT_EQ(ka, kb);
        EXPECT_EQ(PipelineKey::Hash{}(ka), PipelineKey::Hash{}(kb));
        PipelineManager pipelines(device, pass);
        auto one = pipelines.create_pipeline("a", a, first, vertex, fragment);
        EXPECT_EQ(one, pipelines.create_pipeline("b", b, second, vertex, fragment));
        EXPECT_NE(one, pipelines.create_pipeline(
                           "a", make_layout(false, 2), first, vertex, fragment));
        second.dynamic_state.dynamic_states.clear();
        EXPECT_NE(ka, PipelineKey(b, second, *vertex, *fragment, pass));
        second.viewport.width = std::numeric_limits<float>::quiet_NaN();
        EXPECT_THROW(pipelines.create_pipeline("bad", b, second, vertex, fragment),
            std::invalid_argument);
        second = first;
        second.subpass = 1;
        EXPECT_THROW(pipelines.create_pipeline("bad", b, second, vertex, fragment),
            std::invalid_argument);
        second = first;
        second.vertex_input_state.vertex_bindings.push_back(
            second.vertex_input_state.vertex_bindings.front());
        EXPECT_THROW(pipelines.create_pipeline("bad", b, second, vertex, fragment),
            std::invalid_argument);
        second = first;
        second.vertex_input_state.vertex_attributes.push_back(
            second.vertex_input_state.vertex_attributes.front());
        EXPECT_THROW(pipelines.create_pipeline("bad", b, second, vertex, fragment),
            std::invalid_argument);
        b.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 32, 16));
        EXPECT_THROW(pipelines.create_pipeline("bad", b, first, vertex, fragment),
            std::invalid_argument);
    }

    TEST_F(ShaderPipelineTest, ComparesAllStateEvenWhenHashesCollide) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderPass pass(device);
        Shader vertex(device, "vertex", PIPELINE_TRIANGLE_VERT);
        Shader fragment(device, "fragment", PIPELINE_COLOR_FRAG);
        const PipelineKey base({}, {}, vertex, fragment, pass);
        struct SameHash {
            size_t operator()(const PipelineKey&) const { return 0; }
        };
        std::unordered_map<PipelineKey, int, SameHash> cache;
        cache.emplace(base, 0);
        const std::vector<std::function<void(PipelineKey&)>> changes{
            [](auto& k) { ++k.vertex.words.back(); },
            [](auto& k) { k.vertex.entry_point = "other"; },
            [](auto& k) { ++k.fragment.words.back(); },
            [](auto& k) { k.fragment.entry_point = "other"; },
            [](auto& k) {
                k.descriptor_sets = {{{0, vk::DescriptorType::eSampler, 1,
                    vk::ShaderStageFlagBits::eFragment}}};
            },
            [](auto& k) {
                k.push_constants = {{vk::ShaderStageFlagBits::eVertex, 0, 16}};
            },
            [](auto& k) {
                k.config.vertex_input_state.vertex_bindings = {
                    {0, 12, vk::VertexInputRate::eVertex}};
            },
            [](auto& k) {
                k.config.vertex_input_state.vertex_attributes = {
                    {0, 0, vk::Format::eR32Sfloat, 0}};
            },
            [](auto& k) { k.config.input_assembly_state.topology = Topology::LineList; },
            [](auto& k) {
                k.config.input_assembly_state.primitive_restart_enable = true;
            },
            [](auto& k) { k.config.rasterization_state.depth_clamp_enable = true; },
            [](auto& k) {
                k.config.rasterization_state.rasterizer_discard_enable = true;
            },
            [](auto& k) {
                k.config.rasterization_state.polygon_mode = PolygonMode::Line;
            },
            [](auto& k) { k.config.rasterization_state.cull_mode = CullMode::Back; },
            [](auto& k) { k.config.rasterization_state.front_face = FrontFace::CCW; },
            [](auto& k) { k.config.rasterization_state.depth_bias_enable = true; },
            [](auto& k) { k.config.rasterization_state.depth_bias_constant_factor = 1; },
            [](auto& k) { k.config.rasterization_state.depth_bias_clamp = 1; },
            [](auto& k) { k.config.rasterization_state.depth_bias_slope_factor = 1; },
            [](auto& k) { k.config.rasterization_state.line_width = 2; },
            [](auto& k) {
                k.config.multisample_state.rasterization_samples = SampleCount::Count4;
            },
            [](auto& k) { k.config.multisample_state.sample_shading_enable = true; },
            [](auto& k) { k.config.multisample_state.min_sample_shading = 0.5f; },
            [](auto& k) { k.config.depth_stencil_state.depth_test_enable = true; },
            [](auto& k) { k.config.depth_stencil_state.depth_write_enable = true; },
            [](auto& k) {
                k.config.depth_stencil_state.depth_compare_op = CompareOp::Less;
            },
            [](auto& k) { k.config.depth_stencil_state.depth_bounds_test_enable = true; },
            [](auto& k) { k.config.depth_stencil_state.stencil_test_enable = true; },
            [](auto& k) { k.config.viewport.width = 90; },
            [](auto& k) { k.config.scissor.extent.width = 90; },
            [](auto& k) { k.config.color_blend_state.blendEnable = true; },
            [](auto& k) {
                k.config.dynamic_state.dynamic_states = {vk::DynamicState::eViewport};
            },
            [](auto& k) { k.config.subpass = 1; },
            [](auto& k) { k.attachments[0].format = Format::R8G8B8A8_UNORM; },
            [](auto& k) { k.attachments[0].samples = SampleCount::Count4; },
            [](auto& k) { k.render_pass = nullptr; }};
        for(size_t index = 0; index < changes.size(); ++index) {
            auto key = base;
            changes[index](key);
            EXPECT_NE(key, base) << index;
            EXPECT_TRUE(cache.emplace(key, static_cast<int>(index + 1)).second) << index;
            EXPECT_EQ(cache.at(key), index + 1);
        }
        auto negative_zero = base;
        negative_zero.config.rasterization_state.depth_bias_clamp = -0.0f;
        EXPECT_EQ(base, negative_zero);
        EXPECT_EQ(PipelineKey::Hash{}(base), PipelineKey::Hash{}(negative_zero));
    }

    TEST_F(ShaderPipelineTest, ReleasesPipelineAfterItsLastFrameOwnerCompletes) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderPass pass(device);
        PipelineManager pipelines(device, pass);
        auto vertex = std::make_shared<Shader>(device, "vertex", PIPELINE_TRIANGLE_VERT);
        auto fragment = std::make_shared<Shader>(device, "fragment", PIPELINE_COLOR_FRAG);
        auto pipeline = pipelines.create_pipeline("frame", {}, {}, vertex, fragment);
        const std::weak_ptr<Pipeline> old = pipeline;
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        auto& command = frames.get_current_command_buffer();
        command.begin();
        command.bind_pipeline(*pipeline);
        frames.retain_current_frame_resource(pipeline);
        command.end();
        static_cast<void>(device.get_graphics_queue().submit2({}, std::span(&command, 1),
            {}, &frames.get_current_frame_slot().in_flight_fence));
        frames.record_submission();
        frames.end_frame();
        pipeline.reset();
        pipelines.collect_unused();
        EXPECT_FALSE(old.expired());
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 1u);
        frames.wait_for_all_slots();
        EXPECT_TRUE(old.expired());
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
    }

    TEST_F(ShaderPipelineTest, StaticViewportAndScissorChangeActualPixels) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::TransferSrcOptimal;
        color.usage |= ImageUsage::CopySrc;
        RenderPass pass(device, {color},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}}, Format::R8G8B8A8_UNORM);
        auto target = RenderTarget::create_multi_target(device, pass, {32, 16}, 2);
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        auto vertex = std::make_shared<Shader>(device, "vertex", PIPELINE_TRIANGLE_VERT);
        auto fragment = std::make_shared<Shader>(device, "fragment", PIPELINE_COLOR_FRAG);
        PipelineConfig left;
        left.viewport = vk::Viewport(0, 16, 16, -16, 0, 1);
        left.scissor = vk::Rect2D({0, 0}, {8, 16});
        auto right = left;
        right.viewport.x = 16;
        right.scissor.offset.x = 24;
        const std::array draws{
            pipelines.create_pipeline("region", {}, left, vertex, fragment),
            pipelines.create_pipeline("region", {}, right, vertex, fragment)};
        ASSERT_NE(draws[0], draws[1]);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        vk::UniqueDeviceMemory memory;
        auto readback =
            device.get().createBufferUnique(vk::BufferCreateInfo({}, 2 * 32 * 16 * 4,
                vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));
        const auto requirements = device.get().getBufferMemoryRequirements(*readback);
        const auto properties =
            context.get_context().get_physical_device().getMemoryProperties();
        const auto required = vk::MemoryPropertyFlagBits::eHostVisible
                              | vk::MemoryPropertyFlagBits::eHostCoherent;
        std::optional<uint32_t> memory_type;
        for(uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
            if((requirements.memoryTypeBits & (1u << index))
                && (properties.memoryTypes[index].propertyFlags & required) == required) {
                memory_type = index;
                break;
            }
        }
        ASSERT_TRUE(memory_type);
        memory = device.get().allocateMemoryUnique(
            vk::MemoryAllocateInfo(requirements.size, *memory_type));
        device.get().bindBufferMemory(*readback, *memory, 0);
        for(uint32_t index = 0; index < 2; ++index) {
            frames.wait_for_current_slot();
            frames.begin_frame(index);
            auto& command = frames.get_current_command_buffer();
            command.begin();
            target->begin_render_target(command, index);
            command.bind_pipeline(*draws[index]);
            frames.retain_current_frame_resource(draws[index]);
            command.draw(3);
            target->end_render_target(command);
            vk::MemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
                vk::AccessFlagBits::eTransferRead);
            command.get().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
            vk::BufferImageCopy copy;
            copy.bufferOffset = index * 32 * 16 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(32, 16, 1);
            command.get().copyImageToBuffer(
                target->get_color_view(index)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
            barrier = vk::MemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            static_cast<void>(
                device.get_graphics_queue().submit2({}, std::span(&command, 1), {},
                    &frames.get_current_frame_slot().in_flight_fence));
            frames.record_submission();
            frames.end_frame();
        }
        frames.wait_for_all_slots();
        const auto* pixels = static_cast<const uint8_t*>(
            device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        for(uint32_t frame = 0; frame < 2; ++frame) {
            for(uint32_t x = 0; x < 32; ++x) {
                const auto offset = (frame * 32 * 16 + 8 * 32 + x) * 4;
                const bool red = (frame == 0 && x < 8) || (frame == 1 && x >= 24);
                EXPECT_EQ(pixels[offset], red ? 255 : 0) << frame << ":" << x;
                EXPECT_EQ(pixels[offset + 1], 0);
                EXPECT_EQ(pixels[offset + 2], 0);
            }
        }
        device.get().unmapMemory(*memory);
    }

    TEST_F(ShaderPipelineTest, SelectsSubpassAsPartOfPipelineState) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        RenderPass pass(device,
            {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}},
                RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}},
            Format::R8G8B8A8_UNORM);
        PipelineManager pipelines(device, pass);
        auto vertex = std::make_shared<Shader>(device, "vertex", PIPELINE_TRIANGLE_VERT);
        auto fragment = std::make_shared<Shader>(device, "fragment", PIPELINE_COLOR_FRAG);
        PipelineConfig config;
        const auto first =
            pipelines.create_pipeline("pass", {}, config, vertex, fragment);
        config.subpass = 1;
        const auto second =
            pipelines.create_pipeline("pass", {}, config, vertex, fragment);
        EXPECT_NE(first, second);
        EXPECT_EQ(
            second, pipelines.create_pipeline("pass1", {}, config, vertex, fragment));
    }
}
