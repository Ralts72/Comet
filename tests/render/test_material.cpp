#include "render/material.h"

#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

namespace Comet::Tests {
    TEST(MaterialTest, AdvancesRevisionOnlyForChangedValidProperties) {
        Material material("textured", "test");
        EXPECT_EQ(material.get_name(), "textured");
        EXPECT_EQ(material.get_template_name(), "test");
        EXPECT_FALSE(material.get_texture_property("missing"));
        EXPECT_FALSE(material.get_scalar_property("missing"));
        EXPECT_FALSE(material.get_vector_property("missing"));

        const auto initial = material.get_revision();
        material.set_texture_property("albedo", nullptr);
        EXPECT_TRUE(material.set_scalar_property("blend", 0.25f));
        const Math::Vec4 tint(0.2f, 0.4f, 0.6f, 0.8f);
        EXPECT_TRUE(material.set_vector_property("tint", tint));
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_EQ(material.get_scalar_property("blend"), 0.25f);
        EXPECT_EQ(material.get_vector_property("tint"), tint);

        material.set_texture_property("albedo", nullptr);
        EXPECT_TRUE(material.set_scalar_property("blend", 0.25f));
        EXPECT_TRUE(material.set_vector_property("tint", tint));
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_FALSE(material.set_scalar_property("blend", std::numeric_limits<float>::infinity()));
        EXPECT_FALSE(material.set_vector_property(
            "tint", {0, 0, std::numeric_limits<float>::quiet_NaN(), 1}));
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_EQ(material.get_scalar_property("blend"), 0.25f);
        EXPECT_EQ(material.get_vector_property("tint"), tint);
    }

    TEST(MaterialTest, RejectsNonFiniteComponentsWithoutCreatingOrMutatingProperties) {
        Material material("finite", "test");
        ASSERT_TRUE(material.set_scalar_property("scalar", 0.5f));
        const Math::Vec4 original(0.1f, 0.2f, 0.3f, 1.0f);
        ASSERT_TRUE(material.set_vector_property("vector", original));
        const auto revision = material.get_revision();
        for(const float invalid : {std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            EXPECT_FALSE(material.set_scalar_property("scalar", invalid));
            EXPECT_FALSE(material.set_scalar_property("new_scalar", invalid));
            for(int component = 0; component < 4; ++component) {
                auto value = original;
                value[component] = invalid;
                EXPECT_FALSE(material.set_vector_property("vector", value));
                EXPECT_FALSE(material.set_vector_property("new_vector", value));
            }
            EXPECT_EQ(material.get_revision(), revision);
            EXPECT_EQ(material.get_scalar_property("scalar"), 0.5f);
            EXPECT_EQ(material.get_vector_property("vector"), original);
            EXPECT_FALSE(material.get_scalar_property("new_scalar"));
            EXPECT_FALSE(material.get_vector_property("new_vector"));
        }
        EXPECT_TRUE(material.set_scalar_property("scalar", 0.75f));
        EXPECT_EQ(material.get_revision(), revision + 1);
    }

    TEST(MaterialLayoutTest, BuiltinLayoutsShareIdentityAndCarryAuthoringMetadata) {
        const auto textured = MaterialLayout::find_builtin("unlit_texture_blend");
        ASSERT_TRUE(textured);
        EXPECT_EQ(textured, MaterialLayout::find_builtin("unlit_texture_blend"));
        EXPECT_EQ(textured->get_scalars().front().display_name, "Blend");
        EXPECT_FLOAT_EQ(textured->get_scalars().front().min_value, 0);
        EXPECT_FLOAT_EQ(textured->get_scalars().front().max_value, 1);
        EXPECT_EQ(textured->get_vectors().front().semantic,
            MaterialLayout::VectorProperty::Semantic::Color);
        const auto solid = MaterialLayout::find_builtin("unlit_color");
        ASSERT_TRUE(solid);
        EXPECT_TRUE(solid->get_textures().empty());
        EXPECT_FALSE(MaterialLayout::find_builtin("unknown"));
        EXPECT_FALSE(MaterialLayout::create(
            "invalid", {}, 16, std::vector<MaterialLayout::ScalarProperty>{{"x", 0, 0, 1, 0}}));
        EXPECT_FALSE(MaterialLayout::create(
            "invalid", {}, 16, std::vector<MaterialLayout::ScalarProperty>{{"x", 0, 0, 0, 1, 0}}));
    }

    TEST(MaterialLayoutTest, ValidatesSlotsWithoutChangingAuthoringOrder) {
        static_assert(!std::is_copy_assignable_v<Material>);
        static_assert(!std::is_move_assignable_v<Material>);
        static_assert(!std::is_copy_assignable_v<MaterialLayout>);
        auto layout_result = MaterialLayout::create("textured", {{"detail", 7}, {"albedo", 1}});
        ASSERT_TRUE(layout_result) << layout_result.error();
        const auto layout = std::move(layout_result).value();
        EXPECT_EQ(layout.get_textures().front().name, "detail");
        EXPECT_EQ(layout.get_textures().back().name, "albedo");
        EXPECT_FALSE(MaterialLayout::create("", {}));
        EXPECT_FALSE(MaterialLayout::create("test", {{"", 2}}));
        EXPECT_FALSE(MaterialLayout::create("test", {{"a", 2}, {"a", 3}}));
        EXPECT_FALSE(MaterialLayout::create("test", {{"a", 2}, {"b", 2}}));
    }

    TEST(MaterialLayoutTest, RejectsInvalidParameterMemoryLayouts) {
        using Scalars = std::vector<MaterialLayout::ScalarProperty>;
        using Vectors = std::vector<MaterialLayout::VectorProperty>;
        EXPECT_FALSE(MaterialLayout::create("test", {}, 17));
        EXPECT_FALSE(MaterialLayout::create("test", {{"texture", 0}}, 16));
        EXPECT_FALSE(MaterialLayout::create("test", {}, 16, Scalars{{"x", 16, 1}}));
        EXPECT_FALSE(MaterialLayout::create("test", {}, 16, Scalars{{"x", 2, 1}}));
        EXPECT_FALSE(MaterialLayout::create("test", {}, 32, {}, Vectors{{"v", 4, {}}}));
        EXPECT_FALSE(
            MaterialLayout::create("test", {}, 32, Scalars{{"x", 4, 1}}, Vectors{{"v", 0, {}}}));
        EXPECT_FALSE(
            MaterialLayout::create("test", {}, 32, Scalars{{"v", 16, 1}}, Vectors{{"v", 0, {}}}));
    }

}
