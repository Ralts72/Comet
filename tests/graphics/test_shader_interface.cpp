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
#include "specialization_vert.h"
#include "specialization_frag.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/command/command_buffer.h"
#include "graphics/queue.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "graphics/context.h"

#include <gtest/gtest.h>
#include <bit>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>

namespace Comet::Tests {
    TEST(ShaderInterfaceTest, ReflectsTypedSpecializationAndNormalizesExactDefaults) {
        auto shader_result = ShaderInterface::reflect(SPECIALIZATION_FRAG);
        ASSERT_TRUE(shader_result) << shader_result.error();
        const auto shader = std::move(shader_result).value();
        const auto& constants = shader.get_specialization_constants();
        ASSERT_EQ(constants.size(), 4u);
        EXPECT_EQ(constants[0].id, 0u);
        EXPECT_EQ(constants[0].name, "enabled");
        EXPECT_EQ(constants[0].default_value, ShaderInterface::ConstantValue(true));
        EXPECT_EQ(constants[1].default_value, ShaderInterface::ConstantValue(1.0f));
        EXPECT_EQ(constants[2].default_value, ShaderInterface::ConstantValue(int32_t(0)));
        EXPECT_EQ(constants[3].default_value, ShaderInterface::ConstantValue(uint32_t(0)));
        ShaderInterface::Specialization defaults{
            {0, true}, {1, 1.0f}, {2, int32_t(0)}, {3, uint32_t(0)}};
        ASSERT_TRUE(shader.canonicalize_specialization(defaults));
        EXPECT_TRUE(defaults.empty());
        ShaderInterface::Specialization changed{
            {0, false}, {1, -0.0f}, {2, int32_t(-1)}, {3, uint32_t(2)}};
        const auto original = changed;
        ASSERT_TRUE(shader.canonicalize_specialization(changed));
        EXPECT_EQ(changed, original);
        EXPECT_NE(ShaderInterface::ConstantValue(0.0f), ShaderInterface::ConstantValue(-0.0f));
        EXPECT_NE(ShaderInterface::ConstantValue(0u), ShaderInterface::ConstantValue(0));
        const auto nan = std::bit_cast<float>(uint32_t(0x7fc00001));
        EXPECT_EQ(ShaderInterface::ConstantValue(nan), ShaderInterface::ConstantValue(nan));
        ShaderInterface::Specialization wrong{{0, 1u}};
        EXPECT_FALSE(shader.canonicalize_specialization(wrong));
        wrong = {{0, true}, {999, true}};
        const auto rejected = wrong;
        EXPECT_FALSE(shader.canonicalize_specialization(wrong));
        EXPECT_EQ(wrong, rejected);
    }

    TEST(ShaderInterfaceTest, ReflectsProductionStagesBindingsAndPushConstants) {
        auto vertex_result = ShaderInterface::reflect(MATERIAL_MESH_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        const auto vertex = std::move(vertex_result).value();
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

        auto fragment_result = ShaderInterface::reflect(MATERIAL_TEXTURED_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        const auto fragment = std::move(fragment_result).value();
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
            EXPECT_EQ(fragment.get_bindings()[index].type, DescriptorType::CombinedImageSampler);
        }
        const auto debug_vertex = ShaderInterface::reflect(DEBUG_LINE_VERT);
        ASSERT_TRUE(debug_vertex) << debug_vertex.error();
        EXPECT_TRUE(debug_vertex.value().get_bindings().empty());
        EXPECT_EQ(debug_vertex.value().get_push_constants().size(), 1u);
        const auto debug_fragment = ShaderInterface::reflect(DEBUG_LINE_FRAG);
        ASSERT_TRUE(debug_fragment) << debug_fragment.error();
        EXPECT_TRUE(debug_fragment.value().get_bindings().empty());
    }

    TEST(ShaderInterfaceTest, OwnsReflectedValuesAfterInputAndParserAreGone) {
        const auto reflect = [] {
            auto temporary =
                std::vector<uint32_t>(MATERIAL_TEXTURED_FRAG.begin(), MATERIAL_TEXTURED_FRAG.end());
            return ShaderInterface::reflect(temporary);
        };
        auto interface_result = reflect();
        ASSERT_TRUE(interface_result) << interface_result.error();
        const auto interface = std::move(interface_result).value();
        EXPECT_EQ(interface.get_bindings()[0].members[0].name, "tint");
        EXPECT_TRUE(MaterialLayout::find_builtin("unlit_texture_blend")->validate(interface));
    }

    TEST(ShaderInterfaceTest, RejectsMalformedCodeMissingEntryAndRuntimeArrays) {
        EXPECT_FALSE(ShaderInterface::reflect(std::span<const uint32_t>{}));
        EXPECT_FALSE(ShaderInterface::reflect(std::array<uint32_t, 5>{}));
        EXPECT_FALSE(ShaderInterface::reflect(MATERIAL_MESH_VERT, "missing"));
        EXPECT_FALSE(ShaderInterface::reflect(MATERIAL_MESH_VERT, ""));
        EXPECT_FALSE(ShaderInterface::reflect(MATERIAL_MESH_VERT, std::string("main\0other", 10)));
        EXPECT_FALSE(ShaderInterface::reflect(RUNTIME_ARRAY_FRAG));
        auto truncated = std::span(MATERIAL_MESH_VERT).first(6);
        EXPECT_FALSE(ShaderInterface::reflect(truncated));
        auto invalid_instruction =
            std::vector<uint32_t>(MATERIAL_MESH_VERT.begin(), MATERIAL_MESH_VERT.end());
        invalid_instruction[5] = 0;
        EXPECT_FALSE(ShaderInterface::reflect(invalid_instruction));
    }

    TEST(ShaderInterfaceTest, ChecksMaterialBlockOffsetsFormatsAndTextureBindings) {
        auto textured_result = ShaderInterface::reflect(MATERIAL_TEXTURED_FRAG);
        ASSERT_TRUE(textured_result) << textured_result.error();
        const auto textured = std::move(textured_result).value();
        auto solid_result = ShaderInterface::reflect(MATERIAL_SOLID_FRAG);
        ASSERT_TRUE(solid_result) << solid_result.error();
        const auto solid = std::move(solid_result).value();
        EXPECT_TRUE(MaterialLayout::find_builtin("unlit_texture_blend")->validate(textured));
        EXPECT_TRUE(MaterialLayout::find_builtin("unlit_color")->validate(solid));
        EXPECT_FALSE(MaterialLayout::find_builtin("unlit_texture_blend")->validate(solid));
        {
            auto candidate = ShaderInterface::reflect(MATERIAL_INTEGER_FRAG);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_FALSE(MaterialLayout::find_builtin("unlit_color")->validate(candidate.value()));
        }
        auto wrong_size_result = MaterialLayout::create(
            "size", {}, 48, {{"intensity", 16, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(wrong_size_result) << wrong_size_result.error();
        const auto wrong_size = std::move(wrong_size_result).value();
        EXPECT_FALSE(wrong_size.validate(solid));
        auto wrong_offset_result = MaterialLayout::create(
            "offset", {}, 32, {{"intensity", 20, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(wrong_offset_result) << wrong_offset_result.error();
        const auto wrong_offset = std::move(wrong_offset_result).value();
        EXPECT_FALSE(wrong_offset.validate(solid));
        auto missing_member_result =
            MaterialLayout::create("member", {}, 32, {}, {{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(missing_member_result) << missing_member_result.error();
        const auto missing_member = std::move(missing_member_result).value();
        EXPECT_FALSE(missing_member.validate(solid));
        auto wrong_binding_result = MaterialLayout::create(
            "binding", {{"a", 1}, {"b", 3}}, 32, {{"blend", 16, 1}}, {{"tint", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(wrong_binding_result) << wrong_binding_result.error();
        const auto wrong_binding = std::move(wrong_binding_result).value();
        EXPECT_FALSE(wrong_binding.validate(textured));
    }

    using ShaderPipelineTest = EngineTest;

    TEST_F(ShaderPipelineTest, ValidatesArrayCountTypeVisibilityAndNonzeroPushOffset) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto shader_result = ShaderInterface::reflect(INTERFACE_ARRAY_VERT);
        ASSERT_TRUE(shader_result) << shader_result.error();
        const auto shader = std::move(shader_result).value();
        ASSERT_EQ(shader.get_bindings().size(), 1u);
        EXPECT_EQ(shader.get_bindings()[0].count, 4u);
        EXPECT_EQ(shader.get_bindings()[0].set, 3u);
        EXPECT_TRUE(shader.get_bindings()[0].stages == ShaderStage::Vertex);
        ASSERT_EQ(shader.get_push_constants().size(), 1u);
        EXPECT_TRUE(shader.get_push_constants()[0].stages == ShaderStage::Vertex);
        EXPECT_EQ(shader.get_push_constants()[0].offset, 16u);
        EXPECT_EQ(shader.get_push_constants()[0].size, 20u);

        auto empty_result = DescriptorSetLayout::create(device, DescriptorSetLayoutBindings{});
        ASSERT_TRUE(empty_result) << empty_result.error();
        auto empty = std::move(empty_result).value();
        ShaderLayout layout;
        layout.descriptor_set_layouts = {empty, empty, empty, empty};
        layout.push_constants = {std::make_shared<PushConstantRange>(ShaderStage::Vertex, 16, 20)};
        const auto set_binding = [&](DescriptorType type, uint32_t count, ShaderStage stage) {
            DescriptorSetLayoutBindings bindings;
            bindings.add_binding(4, type, Flags<ShaderStage>(stage), count);
            auto result = DescriptorSetLayout::create(device, bindings);
            if(result)
                layout.descriptor_set_layouts[3] = result.value();
            return result;
        };
        EXPECT_FALSE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Vertex));
        EXPECT_TRUE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::CombinedImageSampler, 3, ShaderStage::Vertex));
        EXPECT_FALSE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::CombinedImageSampler, 8, ShaderStage::Vertex));
        EXPECT_TRUE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::SampledImage, 4, ShaderStage::Vertex));
        EXPECT_FALSE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Fragment));
        EXPECT_FALSE(layout.validate(shader));
        ASSERT_TRUE(set_binding(DescriptorType::CombinedImageSampler, 4, ShaderStage::Vertex));

        layout.push_constants = {std::make_shared<PushConstantRange>(ShaderStage::Vertex, 16, 12)};
        EXPECT_FALSE(layout.validate(shader));
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 16, 20)};
        EXPECT_FALSE(layout.validate(shader));
        layout.push_constants = {std::make_shared<PushConstantRange>(ShaderStage::Vertex, 20, 20)};
        EXPECT_FALSE(layout.validate(shader));
        layout.push_constants = {std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 36)};
        EXPECT_TRUE(layout.validate(shader));
        layout.push_constants.clear();
        EXPECT_FALSE(layout.validate(shader));
    }

    TEST_F(ShaderPipelineTest, AcceptsDynamicBufferLayoutsAndUnusedBindings) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto shader_result = ShaderInterface::reflect(MATERIAL_MESH_VERT);
        ASSERT_TRUE(shader_result) << shader_result.error();
        const auto shader = std::move(shader_result).value();
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::UniformBufferDynamic,
            Flags<ShaderStage>(ShaderStage::Vertex) | ShaderStage::Fragment);
        bindings.add_binding(7, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
        ShaderLayout layout;
        auto set_layout = DescriptorSetLayout::create(device, bindings);
        ASSERT_TRUE(set_layout) << set_layout.error();
        layout.descriptor_set_layouts = {set_layout.value()};
        layout.push_constants = {std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 64)};
        EXPECT_TRUE(layout.validate(shader));
    }

    TEST_F(ShaderPipelineTest, RejectsIncompatibleLayoutsBeforeVulkanCreationOrCacheHit) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto pass_result =
            RenderPass::create(device, {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM)},
                {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}}, Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto vertex_result = Shader::create(device, "line", DEBUG_LINE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "line", DEBUG_LINE_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        ShaderLayout layout;
        PipelineConfig config;
        EXPECT_FALSE(pipelines.create_pipeline("line", layout, config, vertex, fragment));
        EXPECT_FALSE(pipelines.create_pipeline("line", layout, config, fragment, vertex));
        EXPECT_FALSE(pipelines.create_pipeline("line", layout, config, nullptr, fragment));
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 64));
        config.vertex_input_state.vertex_bindings = {{0, 28, vk::VertexInputRate::eVertex}};
        config.vertex_input_state.vertex_attributes = {
            {0, 0, vk::Format::eR32G32B32Sfloat, 0}, {1, 0, vk::Format::eR32G32B32A32Sfloat, 12}};
        config.input_assembly_state.topology = Topology::LineList;
        config.dynamic_state.dynamic_states = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        auto valid_result = pipelines.create_pipeline("line", layout, config, vertex, fragment);
        ASSERT_TRUE(valid_result) << valid_result.error();
        auto valid = std::move(valid_result).value();
        ASSERT_TRUE(valid);
        layout.push_constants.clear();
        EXPECT_FALSE(pipelines.create_pipeline("line", layout, config, vertex, fragment));
        EXPECT_FALSE(Shader::create(device, "bad", std::span<const uint32_t>{}));

        auto material_result = ShaderInterface::reflect(MATERIAL_TEXTURED_FRAG);
        ASSERT_TRUE(material_result) << material_result.error();
        const auto material = std::move(material_result).value();
        EXPECT_FALSE(layout.validate(material));
        DescriptorSetLayoutBindings empty;
        auto empty_set_result = DescriptorSetLayout::create(device, empty);
        ASSERT_TRUE(empty_set_result) << empty_set_result.error();
        auto empty_set = std::move(empty_set_result).value();
        layout.descriptor_set_layouts = {empty_set, empty_set};
        EXPECT_FALSE(layout.validate(material));
        layout.descriptor_set_layouts = {nullptr};
        {
            auto candidate = ShaderInterface::reflect(DEBUG_LINE_FRAG);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_FALSE(layout.validate(candidate.value()));
        }
        layout.descriptor_set_layouts.clear();
        layout.push_constants = {nullptr};
        {
            auto candidate = ShaderInterface::reflect(DEBUG_LINE_FRAG);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_FALSE(layout.validate(candidate.value()));
        }
    }

    TEST_F(ShaderPipelineTest, UsesContentAndStateInsteadOfShaderAndPipelineLabels) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        ShaderManager shaders(device);
        auto vertex_result = shaders.load_shader("vertex", PIPELINE_TRIANGLE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = shaders.load_shader("fragment", PIPELINE_COLOR_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        ShaderLayout layout;
        PipelineConfig config;
        auto original_result = pipelines.create_pipeline("same", layout, config, vertex, fragment);
        ASSERT_TRUE(original_result) << original_result.error();
        auto original = std::move(original_result).value();
        {
            auto other_shader = shaders.load_shader("other_fragment", PIPELINE_COLOR_FRAG);
            ASSERT_TRUE(other_shader) << other_shader.error();
            auto other_pipeline =
                pipelines.create_pipeline("other", layout, config, vertex, other_shader.value());
            ASSERT_TRUE(other_pipeline) << other_pipeline.error();
            EXPECT_EQ(original, other_pipeline.value());
        }
        config.rasterization_state.front_face = FrontFace::CCW;
        auto changed_result = pipelines.create_pipeline("same", layout, config, vertex, fragment);
        ASSERT_TRUE(changed_result) << changed_result.error();
        auto changed = std::move(changed_result).value();
        EXPECT_NE(original, changed);
        config.viewport.width = 80;
        {
            auto candidate = pipelines.create_pipeline("same", layout, config, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_NE(changed, candidate.value());
        }
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 3u);
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 2u);

        PipelineColorBlendState blend;
        blend.color_write_mask = Flags<ColorWriteMask>(ColorWriteMask::Red);
        config = PipelineConfig{};
        config.set_color_blend_attachment_state(blend);
        EXPECT_EQ(config.color_blend_state.colorWriteMask, vk::ColorComponentFlagBits::eR);
        {
            auto candidate = pipelines.create_pipeline("same", layout, config, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_NE(original, candidate.value());
        }

        {
            auto candidate = shaders.load_shader("fragment", PIPELINE_COLOR_FRAG);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(fragment, candidate.value());
        }
        auto new_fragment_result = shaders.load_shader("fragment", DEBUG_LINE_FRAG);
        ASSERT_TRUE(new_fragment_result) << new_fragment_result.error();
        auto new_fragment = std::move(new_fragment_result).value();
        EXPECT_NE(fragment, new_fragment);
        EXPECT_EQ(fragment->get_code(),
            std::vector<uint32_t>(PIPELINE_COLOR_FRAG.begin(), PIPELINE_COLOR_FRAG.end()));
        auto rejected = shaders.load_shader("fragment", std::span<const uint32_t>{});
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().message.empty());
        {
            auto candidate = shaders.load_shader("fragment", DEBUG_LINE_FRAG);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(new_fragment, candidate.value());
        }
    }

    TEST_F(ShaderPipelineTest, FailedCandidatesPreserveInputsAndAllowRetry) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        ShaderManager shaders(device);
        PipelineManager pipelines(device, pass);
        const auto invalid = shaders.load_shader("vertex", std::span<const uint32_t>{});
        ASSERT_FALSE(invalid);
        EXPECT_FALSE(invalid.error().message.empty());
        EXPECT_FALSE(invalid.error().result);
        auto vertex = shaders.load_shader("vertex", SPECIALIZATION_VERT);
        ASSERT_TRUE(vertex) << vertex.error();
        auto fragment = shaders.load_shader("fragment", SPECIALIZATION_FRAG);
        ASSERT_TRUE(fragment) << fragment.error();

        PipelineConfig config;
        config.dynamic_state.dynamic_states = {
            vk::DynamicState::eScissor, vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        config.viewport.width = 321;
        config.vertex_specialization = {{0, true}};
        config.fragment_specialization = {{0, true}, {999, true}};
        const auto input = config;
        const auto failed_key =
            PipelineKey::create({}, config, *vertex.value(), *fragment.value(), pass);
        ASSERT_FALSE(failed_key);
        EXPECT_NE(failed_key.error().find("999"), std::string::npos);
        EXPECT_EQ(config, input);
        const auto failed_pipeline =
            pipelines.create_pipeline("candidate", {}, config, vertex.value(), fragment.value());
        ASSERT_FALSE(failed_pipeline);
        EXPECT_EQ(failed_pipeline.error().message, failed_key.error());
        EXPECT_FALSE(failed_pipeline.error().result);
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
        EXPECT_EQ(config, input);

        config.fragment_specialization.clear();
        const auto recovered =
            pipelines.create_pipeline("candidate", {}, config, vertex.value(), fragment.value());
        ASSERT_TRUE(recovered) << recovered.error();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 1u);
        EXPECT_FALSE(
            pipelines.create_pipeline("candidate", {}, input, vertex.value(), fragment.value()));
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 1u);
        const auto reused =
            pipelines.create_pipeline("candidate", {}, config, vertex.value(), fragment.value());
        ASSERT_TRUE(reused) << reused.error();
        EXPECT_EQ(recovered.value(), reused.value());
    }

    TEST_F(ShaderPipelineTest, CanonicalizesLayoutsAndDynamicViewportState) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        auto vertex_result = Shader::create(device, "vertex", PIPELINE_TRIANGLE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", PIPELINE_COLOR_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        const auto make_layout = [&](bool reverse,
                                     uint32_t count) -> Result<ShaderLayout, GraphicsError> {
            DescriptorSetLayoutBindings bindings;
            if(reverse) {
                bindings.add_binding(2, DescriptorType::UniformBuffer,
                    Flags<ShaderStage>(ShaderStage::Vertex), count);
                bindings.add_binding(
                    0, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
            } else {
                bindings.add_binding(
                    0, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
                bindings.add_binding(2, DescriptorType::UniformBuffer,
                    Flags<ShaderStage>(ShaderStage::Vertex), count);
            }
            ShaderLayout layout;
            auto set_layout = DescriptorSetLayout::create(device, bindings);
            if(!set_layout)
                return Result<ShaderLayout, GraphicsError>::failure(set_layout.error());
            layout.descriptor_set_layouts.push_back(std::move(set_layout).value());
            layout.push_constants = {
                std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, 16),
                std::make_shared<PushConstantRange>(ShaderStage::Fragment, 16, 16)};
            if(reverse)
                std::ranges::reverse(layout.push_constants);
            return Result<ShaderLayout, GraphicsError>::success(std::move(layout));
        };
        auto a_result = make_layout(false, 1);
        ASSERT_TRUE(a_result) << a_result.error();
        auto a = std::move(a_result).value();
        auto b_result = make_layout(true, 1);
        ASSERT_TRUE(b_result) << b_result.error();
        auto b = std::move(b_result).value();
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
        second.dynamic_state.dynamic_states = {
            vk::DynamicState::eScissor, vk::DynamicState::eViewport, vk::DynamicState::eViewport};
        second.viewport.width = 800;
        second.scissor.extent = vk::Extent2D(800, 600);
        auto ka_result = PipelineKey::create(a, first, *vertex, *fragment, pass);
        ASSERT_TRUE(ka_result) << ka_result.error();
        const auto ka = std::move(ka_result).value();
        auto kb_result = PipelineKey::create(b, second, *vertex, *fragment, pass);
        ASSERT_TRUE(kb_result) << kb_result.error();
        const auto kb = std::move(kb_result).value();
        EXPECT_EQ(ka, kb);
        EXPECT_EQ(PipelineKey::Hash{}(ka), PipelineKey::Hash{}(kb));
        PipelineManager pipelines(device, pass);
        auto one_result = pipelines.create_pipeline("a", a, first, vertex, fragment);
        ASSERT_TRUE(one_result) << one_result.error();
        auto one = std::move(one_result).value();
        {
            auto candidate = pipelines.create_pipeline("b", b, second, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(one, candidate.value());
        }
        {
            auto layout = make_layout(false, 2);
            ASSERT_TRUE(layout) << layout.error();
            auto candidate =
                pipelines.create_pipeline("a", layout.value(), first, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_NE(one, candidate.value());
        }
        second.dynamic_state.dynamic_states.clear();
        {
            auto candidate = PipelineKey::create(b, second, *vertex, *fragment, pass);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_NE(ka, candidate.value());
        }
        second.viewport.width = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(pipelines.create_pipeline("bad", b, second, vertex, fragment));
        second = first;
        second.subpass = 1;
        EXPECT_FALSE(pipelines.create_pipeline("bad", b, second, vertex, fragment));
        second = first;
        second.vertex_input_state.vertex_bindings.push_back(
            second.vertex_input_state.vertex_bindings.front());
        EXPECT_FALSE(pipelines.create_pipeline("bad", b, second, vertex, fragment));
        second = first;
        second.vertex_input_state.vertex_attributes.push_back(
            second.vertex_input_state.vertex_attributes.front());
        EXPECT_FALSE(pipelines.create_pipeline("bad", b, second, vertex, fragment));
        b.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 32, 16));
        EXPECT_FALSE(pipelines.create_pipeline("bad", b, first, vertex, fragment));
    }

    TEST_F(ShaderPipelineTest, ComparesAllStateEvenWhenHashesCollide) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        auto vertex_result = Shader::create(device, "vertex", PIPELINE_TRIANGLE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", PIPELINE_COLOR_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        auto base_result = PipelineKey::create({}, {}, *vertex, *fragment, pass);
        ASSERT_TRUE(base_result) << base_result.error();
        const auto base = std::move(base_result).value();
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
                k.descriptor_sets = {
                    {{0, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment}}};
            },
            [](auto& k) { k.push_constants = {{vk::ShaderStageFlagBits::eVertex, 0, 16}}; },
            [](auto& k) {
                k.config.vertex_input_state.vertex_bindings = {
                    {0, 12, vk::VertexInputRate::eVertex}};
            },
            [](auto& k) {
                k.config.vertex_input_state.vertex_attributes = {{0, 0, vk::Format::eR32Sfloat, 0}};
            },
            [](auto& k) { k.config.input_assembly_state.topology = Topology::LineList; },
            [](auto& k) { k.config.input_assembly_state.primitive_restart_enable = true; },
            [](auto& k) { k.config.rasterization_state.depth_clamp_enable = true; },
            [](auto& k) { k.config.rasterization_state.rasterizer_discard_enable = true; },
            [](auto& k) { k.config.rasterization_state.polygon_mode = PolygonMode::Line; },
            [](auto& k) { k.config.rasterization_state.cull_mode = CullMode::Back; },
            [](auto& k) { k.config.rasterization_state.front_face = FrontFace::CCW; },
            [](auto& k) { k.config.rasterization_state.depth_bias_enable = true; },
            [](auto& k) { k.config.rasterization_state.depth_bias_constant_factor = 1; },
            [](auto& k) { k.config.rasterization_state.depth_bias_clamp = 1; },
            [](auto& k) { k.config.rasterization_state.depth_bias_slope_factor = 1; },
            [](auto& k) { k.config.rasterization_state.line_width = 2; },
            [](auto& k) { k.config.multisample_state.rasterization_samples = SampleCount::Count4; },
            [](auto& k) { k.config.multisample_state.sample_shading_enable = true; },
            [](auto& k) { k.config.multisample_state.min_sample_shading = 0.5f; },
            [](auto& k) { k.config.depth_stencil_state.depth_test_enable = true; },
            [](auto& k) { k.config.depth_stencil_state.depth_write_enable = true; },
            [](auto& k) { k.config.depth_stencil_state.depth_compare_op = CompareOp::Less; },
            [](auto& k) { k.config.depth_stencil_state.depth_bounds_test_enable = true; },
            [](auto& k) { k.config.depth_stencil_state.stencil_test_enable = true; },
            [](auto& k) { k.config.viewport.width = 90; },
            [](auto& k) { k.config.scissor.extent.width = 90; },
            [](auto& k) { k.config.color_blend_state.blendEnable = true; },
            [](auto& k) { k.config.dynamic_state.dynamic_states = {vk::DynamicState::eViewport}; },
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
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto vertex_result = Shader::create(device, "vertex", PIPELINE_TRIANGLE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", PIPELINE_COLOR_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        auto pipeline_result = pipelines.create_pipeline("frame", {}, {}, vertex, fragment);
        ASSERT_TRUE(pipeline_result) << pipeline_result.error();
        auto pipeline = std::move(pipeline_result).value();
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
        const auto submission = frames.submit({}, {});
        ASSERT_TRUE(submission) << submission.error();
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

    TEST_F(ShaderPipelineTest, SpecializationCacheUsesStageTypeAndExactBits) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result = RenderPass::create(device);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto vertex_result = Shader::create(device, "vertex", SPECIALIZATION_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", SPECIALIZATION_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        PipelineConfig config;
        auto original_result = pipelines.create_pipeline("variant", {}, config, vertex, fragment);
        ASSERT_TRUE(original_result) << original_result.error();
        const auto original = std::move(original_result).value();
        config.vertex_specialization = {{0, true}};
        config.fragment_specialization = {{0, true}, {1, 1.0f}, {2, int32_t(0)}, {3, uint32_t(0)}};
        {
            auto candidate = pipelines.create_pipeline("defaults", {}, config, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(original, candidate.value());
        }
        auto base_result = PipelineKey::create({}, {}, *vertex, *fragment, pass);
        ASSERT_TRUE(base_result) << base_result.error();
        const auto base = std::move(base_result).value();
        auto defaults_result = PipelineKey::create({}, config, *vertex, *fragment, pass);
        ASSERT_TRUE(defaults_result) << defaults_result.error();
        const auto defaults = std::move(defaults_result).value();
        EXPECT_EQ(base, defaults);
        EXPECT_EQ(PipelineKey::Hash{}(base), PipelineKey::Hash{}(defaults));
        config.vertex_specialization = {{0, false}};
        auto vertex_key_result = PipelineKey::create({}, config, *vertex, *fragment, pass);
        ASSERT_TRUE(vertex_key_result) << vertex_key_result.error();
        const auto vertex_key = std::move(vertex_key_result).value();
        config.vertex_specialization.clear();
        config.fragment_specialization = {{0, false}};
        auto fragment_key_result = PipelineKey::create({}, config, *vertex, *fragment, pass);
        ASSERT_TRUE(fragment_key_result) << fragment_key_result.error();
        const auto fragment_key = std::move(fragment_key_result).value();
        EXPECT_NE(vertex_key, fragment_key);
        config.fragment_specialization = {{1, 0.0f}};
        auto positive_zero_result = PipelineKey::create({}, config, *vertex, *fragment, pass);
        ASSERT_TRUE(positive_zero_result) << positive_zero_result.error();
        const auto positive_zero = std::move(positive_zero_result).value();
        config.fragment_specialization = {{1, -0.0f}};
        auto negative_zero_result = PipelineKey::create({}, config, *vertex, *fragment, pass);
        ASSERT_TRUE(negative_zero_result) << negative_zero_result.error();
        const auto negative_zero = std::move(negative_zero_result).value();
        EXPECT_NE(positive_zero, negative_zero);
        struct CollisionHash {
            size_t operator()(const PipelineKey&) const { return 0; }
        };
        std::unordered_map<PipelineKey, int, CollisionHash> collisions;
        for(const auto* key : {&base, &vertex_key, &fragment_key, &positive_zero, &negative_zero})
            EXPECT_TRUE(collisions.emplace(*key, 1).second);
        EXPECT_EQ(collisions.size(), 5u);
        config.fragment_specialization = {{1, 0u}};
        EXPECT_FALSE(pipelines.create_pipeline("variant", {}, config, vertex, fragment));
        config.fragment_specialization = {{99, true}};
        EXPECT_FALSE(pipelines.create_pipeline("variant", {}, config, vertex, fragment));
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 1u);
        {
            auto candidate = pipelines.create_pipeline("variant", {}, {}, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(original, candidate.value());
        }
    }

    TEST_F(ShaderPipelineTest, StaticViewportScissorAndSpecializationChangeActualPixels) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::TransferSrcOptimal;
        color.usage |= ImageUsage::CopySrc;
        auto pass_result = RenderPass::create(device, {color},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}}, Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        constexpr uint32_t VARIANT_COUNT = 5;
        auto target_result =
            RenderTarget::try_create_multi_target(device, pass, {32, 16}, VARIANT_COUNT);
        ASSERT_TRUE(target_result) << target_result.error();
        auto target = std::move(target_result).value();
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        auto vertex_result = Shader::create(device, "vertex", SPECIALIZATION_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", SPECIALIZATION_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        PipelineConfig left;
        left.viewport = vk::Viewport(0, 16, 16, -16, 0, 1);
        left.scissor = vk::Rect2D({0, 0}, {8, 16});
        auto right = left;
        right.viewport.x = 16;
        right.scissor.offset.x = 24;
        auto cyan = right;
        cyan.fragment_specialization = {{1, 0.5f}, {2, int32_t(1)}, {3, uint32_t(1)}};
        auto hidden = left;
        hidden.vertex_specialization = {{0, false}};
        auto black = left;
        black.fragment_specialization = {{0, false}};
        std::array<std::shared_ptr<Pipeline>, VARIANT_COUNT> draws;
        const std::array configs{left, right, cyan, hidden, black};
        for(size_t index = 0; index < draws.size(); ++index) {
            auto candidate =
                pipelines.create_pipeline("region", {}, configs[index], vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            draws[index] = std::move(candidate).value();
        }
        ASSERT_NE(draws[0], draws[1]);
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), VARIANT_COUNT);
        {
            auto candidate = pipelines.create_pipeline("reused cyan", {}, cyan, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(draws[2], candidate.value());
        }
        cyan.fragment_specialization = {{1, 1.0f}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(VARIANT_COUNT);
        vk::UniqueDeviceMemory memory;
        auto readback =
            device.get().createBufferUnique(vk::BufferCreateInfo({}, VARIANT_COUNT * 32 * 16 * 4,
                vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));
        const auto requirements = device.get().getBufferMemoryRequirements(*readback);
        const auto properties = context.get_context().get_physical_device().getMemoryProperties();
        const auto required =
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
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
        for(uint32_t index = 0; index < VARIANT_COUNT; ++index) {
            frames.wait_for_current_slot();
            frames.begin_frame(index);
            auto& command = frames.get_current_command_buffer();
            command.begin();
            target->begin_render_target(command, index);
            command.bind_pipeline(*draws[index]);
            frames.retain_current_frame_resource(draws[index]);
            command.draw(3);
            target->end_render_target(command);
            vk::MemoryBarrier barrier(
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
            vk::BufferImageCopy copy;
            copy.bufferOffset = index * 32 * 16 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(32, 16, 1);
            command.get().copyImageToBuffer(target->get_color_view(index)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
            barrier = vk::MemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            const auto submission = frames.submit({}, {});
            ASSERT_TRUE(submission) << submission.error();
            frames.end_frame();
        }
        frames.wait_for_all_slots();
        const auto* pixels =
            static_cast<const uint8_t*>(device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        for(uint32_t frame = 0; frame < VARIANT_COUNT; ++frame) {
            for(uint32_t x = 0; x < 32; ++x) {
                const auto offset = (frame * 32 * 16 + 8 * 32 + x) * 4;
                const bool red = (frame == 0 && x < 8) || (frame == 1 && x >= 24);
                const bool cyan_pixel = frame == 2 && x >= 24;
                int expected_red = 0;
                if(red)
                    expected_red = 255;
                else if(cyan_pixel)
                    expected_red = 128;
                EXPECT_NEAR(pixels[offset], expected_red, 1) << frame << ":" << x;
                EXPECT_EQ(pixels[offset + 1], cyan_pixel ? 255 : 0);
                EXPECT_EQ(pixels[offset + 2], cyan_pixel ? 255 : 0);
            }
        }
        device.get().unmapMemory(*memory);
    }

    TEST_F(ShaderPipelineTest, SelectsSubpassAsPartOfPipelineState) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto pass_result =
            RenderPass::create(device, {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM)},
                {RenderSubPass{{}, {SubpassColorAttachment(0)}, {}},
                    RenderSubPass{{}, {SubpassColorAttachment(0)}, {}}},
                Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto vertex_result = Shader::create(device, "vertex", PIPELINE_TRIANGLE_VERT);
        ASSERT_TRUE(vertex_result) << vertex_result.error();
        auto vertex = std::move(vertex_result).value();
        auto fragment_result = Shader::create(device, "fragment", PIPELINE_COLOR_FRAG);
        ASSERT_TRUE(fragment_result) << fragment_result.error();
        auto fragment = std::move(fragment_result).value();
        PipelineConfig config;
        auto first_result = pipelines.create_pipeline("pass", {}, config, vertex, fragment);
        ASSERT_TRUE(first_result) << first_result.error();
        const auto first = std::move(first_result).value();
        config.subpass = 1;
        auto second_result = pipelines.create_pipeline("pass", {}, config, vertex, fragment);
        ASSERT_TRUE(second_result) << second_result.error();
        const auto second = std::move(second_result).value();
        EXPECT_NE(first, second);
        {
            auto candidate = pipelines.create_pipeline("pass1", {}, config, vertex, fragment);
            ASSERT_TRUE(candidate) << candidate.error();
            EXPECT_EQ(second, candidate.value());
        }
    }
}
