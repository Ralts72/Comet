#include "graphics/pipeline/shader_interface.h"
#include "graphics/pipeline/shader.h"
#include "core/engine.h"
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
        EXPECT_EQ(vertex.get_stage(), vk::ShaderStageFlagBits::eVertex);
        ASSERT_EQ(vertex.get_bindings().size(), 1u);
        const auto& frame = vertex.get_bindings().front();
        EXPECT_EQ(frame.set, 0u);
        EXPECT_EQ(frame.binding, 0u);
        EXPECT_EQ(frame.count, 1u);
        EXPECT_EQ(frame.block_size, 128u);
        EXPECT_EQ(frame.type, vk::DescriptorType::eUniformBuffer);
        ASSERT_EQ(vertex.get_push_constants().size(), 1u);
        EXPECT_EQ(vertex.get_push_constants()[0],
            vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, 64));

        const ShaderInterface fragment(MATERIAL_TEXTURED_FRAG);
        EXPECT_EQ(fragment.get_stage(), vk::ShaderStageFlagBits::eFragment);
        ASSERT_EQ(fragment.get_bindings().size(), 3u);
        EXPECT_TRUE(fragment.get_push_constants().empty());
        const auto& parameters = fragment.get_bindings()[0];
        EXPECT_EQ(parameters.set, 1u);
        EXPECT_EQ(parameters.binding, 0u);
        EXPECT_EQ(parameters.block_size, 32u);
        ASSERT_EQ(parameters.members.size(), 2u);
        EXPECT_EQ(parameters.members[0].name, "tint");
        EXPECT_EQ(parameters.members[0].offset, 0u);
        EXPECT_EQ(parameters.members[0].format, vk::Format::eR32G32B32A32Sfloat);
        EXPECT_EQ(parameters.members[1].name, "blend");
        EXPECT_EQ(parameters.members[1].offset, 16u);
        EXPECT_EQ(parameters.members[1].format, vk::Format::eR32Sfloat);
        for(uint32_t index = 1; index <= 2; ++index) {
            EXPECT_EQ(fragment.get_bindings()[index].binding, index);
            EXPECT_EQ(fragment.get_bindings()[index].type,
                vk::DescriptorType::eCombinedImageSampler);
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
            MaterialLayout::find_builtin("cube_texture")->validate(interface));
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
    }

    TEST(ShaderInterfaceTest, ValidatesArrayCountTypeVisibilityAndNonzeroPushOffset) {
        const ShaderInterface shader(INTERFACE_ARRAY_VERT);
        ASSERT_EQ(shader.get_bindings().size(), 1u);
        EXPECT_EQ(shader.get_bindings()[0].count, 4u);
        EXPECT_EQ(shader.get_bindings()[0].set, 3u);
        std::array bindings{
            vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eCombinedImageSampler,
                4, vk::ShaderStageFlagBits::eVertex)};
        EXPECT_NO_THROW(shader.validate_set(3, bindings));
        EXPECT_THROW(shader.validate_set(3, {}), std::invalid_argument);
        bindings[0].descriptorCount = 3;
        EXPECT_THROW(shader.validate_set(3, bindings), std::invalid_argument);
        bindings[0].descriptorCount = 8;
        EXPECT_NO_THROW(shader.validate_set(3, bindings));
        bindings[0].descriptorType = vk::DescriptorType::eSampledImage;
        EXPECT_THROW(shader.validate_set(3, bindings), std::invalid_argument);
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
        EXPECT_THROW(shader.validate_set(3, bindings), std::invalid_argument);

        ASSERT_EQ(shader.get_push_constants().size(), 1u);
        EXPECT_EQ(shader.get_push_constants()[0],
            vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 16, 20));
        std::array ranges{
            vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 16, 20)};
        EXPECT_NO_THROW(shader.validate_push_constants(ranges));
        ranges[0].size = 12;
        EXPECT_THROW(shader.validate_push_constants(ranges), std::invalid_argument);
        ranges[0].size = 20;
        ranges[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
        EXPECT_THROW(shader.validate_push_constants(ranges), std::invalid_argument);
        EXPECT_THROW(shader.validate_push_constants({}), std::invalid_argument);
    }

    TEST(ShaderInterfaceTest, AcceptsDynamicBufferLayoutsAndUnusedBindings) {
        const ShaderInterface shader(MATERIAL_MESH_VERT);
        std::array bindings{
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic,
                1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
            vk::DescriptorSetLayoutBinding(
                7, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment)};
        EXPECT_NO_THROW(shader.validate_set(0, bindings));
    }

    TEST(ShaderInterfaceTest, ChecksMaterialBlockOffsetsFormatsAndTextureBindings) {
        const ShaderInterface textured(MATERIAL_TEXTURED_FRAG);
        const ShaderInterface solid(MATERIAL_SOLID_FRAG);
        EXPECT_NO_THROW(MaterialLayout::find_builtin("cube_texture")->validate(textured));
        EXPECT_NO_THROW(MaterialLayout::find_builtin("unlit_color")->validate(solid));
        EXPECT_THROW(MaterialLayout::find_builtin("cube_texture")->validate(solid),
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
