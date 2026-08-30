#include <gtest/gtest.h>

#include "render/material.h"

namespace Comet::Tests {

TEST(MaterialInstanceTest, KeepsRuntimeMaterialAlive) {
    const auto material = std::make_shared<Material>(
        "default", "test", MaterialConfig{});
    const MaterialInstance instance(material);

    EXPECT_EQ(instance.get_material(), material);
}

TEST(MaterialInstanceTest, RejectsNullMaterial) {
    EXPECT_DEATH({ MaterialInstance instance(nullptr); }, "");
}

TEST(MaterialTest, StoresTextureProperty) {
    Material material("textured", "test", MaterialConfig{});
    const std::shared_ptr<Texture> texture;

    material.set_property_texture("albedo", texture);

    const MaterialProperty& property = material.get_property("albedo");
    EXPECT_EQ(property.type, MaterialPropertyType::Texture);
    EXPECT_EQ(std::get<std::shared_ptr<Texture>>(property.value), texture);
    EXPECT_EQ(material.get_texture_property("albedo"), texture);
    EXPECT_EQ(material.get_texture_property("missing"), nullptr);
}

TEST(MaterialTest, StoresRuntimeTemplateIdentity) {
    const Material material("textured", "cube_texture", MaterialConfig{});

    EXPECT_EQ(material.get_template_name(), "cube_texture");
}

} // namespace Comet::Tests
