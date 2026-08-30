#include <gtest/gtest.h>

#include "render/material.h"

namespace Comet::Tests {
    TEST(MaterialTest, StoresTextureProperties) {
        Material material("textured", "test");
        const std::shared_ptr<Texture> texture;

        material.set_texture_property("albedo", texture);

        EXPECT_EQ(material.get_texture_property("albedo"), texture);
        EXPECT_EQ(material.get_texture_property("missing"), nullptr);
        ASSERT_EQ(material.get_texture_properties().size(), 1u);
        EXPECT_TRUE(material.get_texture_properties().contains("albedo"));
    }

    TEST(MaterialTest, StoresRuntimeIdentity) {
        const Material material("textured", "cube_texture");

        EXPECT_EQ(material.get_name(), "textured");
        EXPECT_EQ(material.get_template_name(), "cube_texture");
    }
}
