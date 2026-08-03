#include <gtest/gtest.h>

#include "render/material.h"

namespace Comet::Tests {

TEST(MaterialManagerTest, CreatesAndFindsMaterialInstances) {
    MaterialManager manager;
    MaterialConfig config;

    const auto material = manager.create_material("default", config);
    const auto instance = manager.create_instance("default");

    ASSERT_NE(material, nullptr);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->get_material(), material);
    EXPECT_EQ(manager.get_material("default"), material);
}

TEST(MaterialManagerTest, MissingMaterialDoesNotCreateInvalidInstance) {
    MaterialManager manager;

    EXPECT_EQ(manager.create_instance("missing"), nullptr);
}

TEST(MaterialInstanceTest, RejectsNullMaterial) {
    EXPECT_DEATH({ MaterialInstance instance(nullptr); }, "");
}

TEST(MaterialTest, StoresTextureProperty) {
    Material material("textured", MaterialConfig{});
    const std::shared_ptr<Texture> texture;

    material.set_property_texture("albedo", texture);

    const MaterialProperty& property = material.get_property("albedo");
    EXPECT_EQ(property.type, MaterialPropertyType::Texture);
    EXPECT_EQ(std::get<std::shared_ptr<Texture>>(property.value), texture);
}

} // namespace Comet::Tests
