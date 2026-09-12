#include "render/material.h"

#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
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
        material.set_scalar_property("blend", 0.25f);
        const Math::Vec4 tint(0.2f, 0.4f, 0.6f, 0.8f);
        material.set_vector_property("tint", tint);
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_EQ(material.get_scalar_property("blend"), 0.25f);
        EXPECT_EQ(material.get_vector_property("tint"), tint);

        material.set_texture_property("albedo", nullptr);
        material.set_scalar_property("blend", 0.25f);
        material.set_vector_property("tint", tint);
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_THROW(
            material.set_scalar_property("blend", std::numeric_limits<float>::infinity()),
            std::invalid_argument);
        EXPECT_THROW(material.set_vector_property(
                         "tint", {0, 0, std::numeric_limits<float>::quiet_NaN(), 1}),
            std::invalid_argument);
        EXPECT_EQ(material.get_revision(), initial + 3);
        EXPECT_EQ(material.get_scalar_property("blend"), 0.25f);
        EXPECT_EQ(material.get_vector_property("tint"), tint);
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
        EXPECT_THROW(MaterialLayout("invalid", {}, 16,
                         std::vector<MaterialLayout::ScalarProperty>{{"x", 0, 0, 1, 0}}),
            std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("invalid", {}, 16,
                std::vector<MaterialLayout::ScalarProperty>{{"x", 0, 0, 0, 1, 0}}),
            std::invalid_argument);
    }

    TEST(MaterialLayoutTest, ValidatesAndOrdersLayoutSlots) {
        static_assert(!std::is_copy_assignable_v<Material>);
        static_assert(!std::is_move_assignable_v<Material>);
        static_assert(!std::is_copy_assignable_v<MaterialLayout>);
        const MaterialLayout layout("textured", {{"detail", 7}, {"albedo", 1}});
        EXPECT_EQ(layout.get_textures().front().name, "albedo");
        EXPECT_THROW(MaterialLayout("", {}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", {{"", 2}}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", {{"a", 2}, {"a", 3}}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", {{"a", 2}, {"b", 2}}), std::invalid_argument);
    }

    TEST(MaterialLayoutTest, RejectsInvalidParameterMemoryLayouts) {
        using Scalars = std::vector<MaterialLayout::ScalarProperty>;
        using Vectors = std::vector<MaterialLayout::VectorProperty>;
        EXPECT_THROW(MaterialLayout("test", {}, 17), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", {{"texture", 0}}, 16), std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", {}, 16, Scalars{{"x", 16, 1}}), std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", {}, 16, Scalars{{"x", 2, 1}}), std::invalid_argument);
        EXPECT_THROW(MaterialLayout("test", {}, 32, {}, Vectors{{"v", 4, {}}}),
            std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", {}, 32, Scalars{{"x", 4, 1}}, Vectors{{"v", 0, {}}}),
            std::invalid_argument);
        EXPECT_THROW(
            MaterialLayout("test", {}, 32, Scalars{{"v", 16, 1}}, Vectors{{"v", 0, {}}}),
            std::invalid_argument);
    }

}
