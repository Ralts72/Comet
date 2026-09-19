#include "assets/material_editing.h"
#include "asset/database.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(MaterialEditingTest, DraftValidationIsIndependentOfInspectorAndRejectsTypedMismatches) {
        Comet::Tests::TemporaryDirectory directory;
        const Comet::AssetDatabase database{Comet::ProjectPaths(directory.path())};
        const auto layout = Comet::MaterialLayout::find_builtin("pbr");
        auto data = make_material_data(*layout);
        EXPECT_TRUE(validate_material_data(data, *layout, database));
        data.texture_properties["metallic"] = Comet::AssetHandle(1);
        EXPECT_FALSE(validate_material_data(data, *layout, database));
        data.texture_properties.clear();
        data.texture_properties["base_color_texture"] = Comet::AssetHandle(1);
        EXPECT_FALSE(validate_material_data(data, *layout, database));
        data.texture_properties.clear();
        data.template_name = "unlit_color";
        EXPECT_FALSE(validate_material_data(data, *layout, database));
    }

    TEST(MaterialEditingTest, PublicTemplatesProduceDefaultsWithoutFakeTextureHandles) {
        for(const auto& layout : Comet::MaterialLayout::builtins()) {
            const auto data = make_material_data(*layout);
            EXPECT_EQ(data.template_name, layout->get_name());
            EXPECT_TRUE(data.texture_properties.empty());
            for(const auto& property : layout->get_scalars())
                EXPECT_EQ(data.scalar_properties.at(property.name), property.default_value);
            for(const auto& property : layout->get_vectors())
                EXPECT_EQ(data.vector_properties.at(property.name), property.default_value);
        }
    }

    TEST(MaterialEditingTest, SwitchingTemplatesDropsIncompatiblePropertiesAndUsesNewDefaults) {
        const auto pbr = Comet::MaterialLayout::find_builtin("pbr");
        const auto unlit = Comet::MaterialLayout::find_builtin("unlit_color");
        auto data = make_material_data(*pbr);
        data.texture_properties["base_color_texture"] = Comet::AssetHandle(42);
        const auto change = change_material_template(data, pbr.get(), *unlit);
        EXPECT_EQ(change.data, make_material_data(*unlit));
        EXPECT_EQ(change.discarded_properties.size(), 4);
        EXPECT_EQ(data.template_name, "pbr");
        EXPECT_EQ(data.texture_properties.at("base_color_texture"), Comet::AssetHandle(42));
    }

    TEST(MaterialEditingTest, MigrationUsesPropertyIdentityTypeAndSemanticNotGpuOffsets) {
        using Layout = Comet::MaterialLayout;
        auto before = Layout::create("before", {{"texture", 1}}, 64,
            {{"value", 0, 1}, {"changed_type", 4, 1}},
            {{"color", 16, {}, Layout::VectorProperty::Semantic::Color},
                {"direction", 32, {}, Layout::VectorProperty::Semantic::Vector}});
        auto after =
            Layout::create("after", {{"texture", 5}}, 80, {{"value", 64, 2}, {"new", 68, 3}},
                {{"color", 0, {}, Layout::VectorProperty::Semantic::Color},
                    {"direction", 16, {}, Layout::VectorProperty::Semantic::Color},
                    {"changed_type", 32, {1, 1, 1, 1}}});
        ASSERT_TRUE(before);
        ASSERT_TRUE(after);
        auto data = make_material_data(before.value());
        data.scalar_properties["value"] = 7;
        data.vector_properties["color"] = {0.2f, 0.3f, 0.4f, 1};
        data.texture_properties["texture"] = Comet::AssetHandle(42);
        const auto change = change_material_template(data, &before.value(), after.value());
        EXPECT_EQ(change.data.scalar_properties.at("value"), 7);
        EXPECT_EQ(change.data.scalar_properties.at("new"), 3);
        EXPECT_EQ(change.data.vector_properties.at("color"), data.vector_properties.at("color"));
        EXPECT_EQ(change.data.vector_properties.at("direction"), Comet::Math::Vec4(0));
        EXPECT_EQ(change.data.vector_properties.at("changed_type"), Comet::Math::Vec4(1));
        EXPECT_EQ(change.data.texture_properties.at("texture"), Comet::AssetHandle(42));
        EXPECT_EQ(change.discarded_properties.size(), 2);
    }

    TEST(MaterialEditingTest, UnknownSourceTemplateDoesNotGuessParameterMeaning) {
        const auto layout = Comet::MaterialLayout::find_builtin("pbr");
        const Comet::MaterialData unknown{
            .template_name = "unknown", .scalar_properties = {{"metallic", 1}}};
        const auto change = change_material_template(unknown, nullptr, *layout);
        EXPECT_EQ(change.data, make_material_data(*layout));
        EXPECT_EQ(change.discarded_properties, std::vector<std::string>{"metallic"});
    }
}
