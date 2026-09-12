#include "graphics/pipeline/shader_interface.h"
#include "graphics/pipeline/shader.h"
#include "core/engine.h"
#include "config/config.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include <algorithm>
#include <array>
#include "render/material_runtime.h"
#include "diagnostics/logger.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"
#include "debug_line_vert.h"
#include "debug_line_frag.h"
#include "interface_array_vert.h"
#include "material_integer_frag.h"
#include "runtime_array_frag.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <stdexcept>

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
            "size", 1, {}, 48, {{"intensity", 16, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_size.validate(solid), std::invalid_argument);
        const MaterialLayout wrong_offset(
            "offset", 1, {}, 32, {{"intensity", 20, 1}}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_offset.validate(solid), std::invalid_argument);
        const MaterialLayout missing_member(
            "member", 1, {}, 32, {}, {{"color", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(missing_member.validate(solid), std::invalid_argument);
        const MaterialLayout wrong_binding("binding", 1, {{"a", 1}, {"b", 3}}, 32,
            {{"blend", 16, 1}}, {{"tint", 0, {1, 1, 1, 1}}});
        EXPECT_THROW(wrong_binding.validate(textured), std::invalid_argument);
    }

    class ShaderPipelineTest: public ::testing::Test {
    protected:
        void SetUp() override {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            engine = std::make_unique<Engine>(config);
        }
        void TearDown() override {
            if(engine)
                engine->get_renderer().get_render_context().wait_idle();
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
        }
        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
    };

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
}
