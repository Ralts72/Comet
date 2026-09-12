#include <gtest/gtest.h>

#include "render/material.h"

namespace Comet::Tests {
    TEST(MaterialTest, StoresRuntimeProperties) {
        Material material("textured", "test");
        const std::shared_ptr<Texture> texture;

        material.set_texture_property("albedo", texture);

        EXPECT_EQ(material.get_name(), "textured");
        EXPECT_EQ(material.get_template_name(), "test");
        EXPECT_EQ(material.get_texture_property("albedo"), texture);
        EXPECT_EQ(material.get_texture_property("missing"), nullptr);
        ASSERT_EQ(material.get_texture_properties().size(), 1u);
        EXPECT_TRUE(material.get_texture_properties().contains("albedo"));

        const Math::Vec4 tint(0.2f, 0.4f, 0.6f, 0.8f);
        material.set_vector_property("tint", tint);
        ASSERT_TRUE(material.get_vector_property("tint"));
        EXPECT_EQ(material.get_vector_property("tint").value(), tint);
        EXPECT_FALSE(material.get_vector_property("missing"));
    }
}
