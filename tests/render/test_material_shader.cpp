#include "render/material/material_shader.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(MaterialShaderTest, BuiltinsHaveCompleteCodeAndValidFixedContracts) {
        const auto shaders = default_material_shaders();
        ASSERT_EQ(shaders.size(), builtin_material_shaders().size());
        for(const auto& definition : builtin_material_shaders()) {
            EXPECT_FALSE(definition.material.empty());
            ASSERT_TRUE(shaders.contains(definition.name));
            const auto& program = shaders.find(definition.name)->second;
            EXPECT_FALSE(program.vertex.empty());
            EXPECT_FALSE(program.fragment.empty());
        }
        const auto valid = validate_material_shaders(shaders);
        EXPECT_TRUE(valid) << valid.error();
    }

    TEST(MaterialShaderTest, RejectsUnknownEmptyIncompleteAndMalformedPrograms) {
        EXPECT_FALSE(validate_material_shaders({}));
        const auto shaders = default_material_shaders();
        const auto& program = shaders.at("unlit_color");
        EXPECT_FALSE(validate_material_shaders({{"unknown", program}}));
        EXPECT_FALSE(validate_material_shaders({{"unlit_color", {}}}));
        EXPECT_FALSE(validate_material_shaders({{"unlit_color", {program.vertex, {}}}}));
        EXPECT_FALSE(validate_material_shaders({{"unlit_color", {{}, program.fragment}}}));
        EXPECT_FALSE(validate_material_shaders({{"unlit_color", {{0}, program.fragment}}}));
        EXPECT_TRUE(validate_material_shaders({{"unlit_color", program}}));
    }

    TEST(MaterialShaderTest, MergeRetainsOtherProgramsAndOwnsPublishedCode) {
        const auto original = default_material_shaders();
        auto combined = original;
        MaterialShaders update{{"unlit_color", original.at("unlit_color")}};
        // SPIR-V generator 字段不改变接口，构造可区分且有效的覆盖。
        update.at("unlit_color").fragment[2] ^= 1;
        ASSERT_TRUE(validate_material_shaders(update));
        const auto expected = update.at("unlit_color").fragment;
        merge_material_shaders(combined, update);
        update.at("unlit_color").fragment.clear();
        EXPECT_EQ(combined.at("unlit_color").fragment, expected);
        EXPECT_EQ(
            combined.at("unlit_texture_blend").vertex, original.at("unlit_texture_blend").vertex);
        EXPECT_EQ(combined.at("unlit_texture_blend").fragment,
            original.at("unlit_texture_blend").fragment);
        EXPECT_EQ(combined.at("lambert").fragment, original.at("lambert").fragment);
    }
}
