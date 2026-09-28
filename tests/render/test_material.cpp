#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "pbr_vert.h"
#include "pbr_frag.h"

#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

namespace Comet::Tests {
    TEST(MaterialLayoutTest, RuntimeOverridesCheckNamesTypesRangesAndFiniteValues) {
        const auto layout = MaterialLayout::find_builtin("pbr");
        MaterialOverrides overrides;
        overrides.scalar_properties["roughness"] = 0.4f;
        overrides.vector_properties["base_color"] = {2, 0.2f, 0.3f, 1};
        ASSERT_TRUE(layout->validate_parameters(overrides));
        for(const float invalid : {-1.0f, 1.1f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()}) {
            overrides.scalar_properties["roughness"] = invalid;
            EXPECT_FALSE(layout->validate_parameters(overrides));
        }
        overrides.scalar_properties["roughness"] = 0.4f;
        overrides.scalar_properties["base_color"] = 1;
        EXPECT_FALSE(layout->validate_parameters(overrides));
        overrides.scalar_properties.erase("base_color");
        overrides.vector_properties["unknown"] = Math::Vec4(1);
        EXPECT_FALSE(layout->validate_parameters(overrides));
        overrides.vector_properties.erase("unknown");
        for(int component = 0; component < 4; ++component) {
            auto& color = overrides.vector_properties["base_color"];
            color = Math::Vec4(1);
            color[component] = std::numeric_limits<float>::quiet_NaN();
            EXPECT_FALSE(layout->validate_parameters(overrides));
        }
        auto unbounded = MaterialLayout::create("unbounded", {}, 16, {{"offset", 0, 0}});
        ASSERT_TRUE(unbounded);
        overrides.scalar_properties = {{"offset", -100}};
        overrides.vector_properties.clear();
        EXPECT_TRUE(unbounded.value().validate_parameters(overrides));
    }

    TEST(MaterialProgramsTest, RuntimeValidationPrefersPublishedContractAfterStartup) {
        AssetRegistry assets;
        MaterialPrograms programs(assets);
        const AssetHandle material_handle(71), program_handle(72);
        MaterialOverrides overrides{.material = material_handle};
        overrides.vector_properties["base_color"] = Math::Vec4(1);
        EXPECT_FALSE(programs.validate(overrides));
        auto material = std::make_shared<Material>("example", "pbr");
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        EXPECT_TRUE(programs.validate(overrides));
        ASSERT_TRUE(assets.replace_asset(
            material_handle, std::make_shared<Material>("example", "pbr", program_handle)));
        EXPECT_FALSE(programs.validate(overrides));
        auto source = std::make_shared<ShaderProgramArtifact>();
        source->handle = program_handle;
        ASSERT_TRUE(assets.register_asset(program_handle, source));
        EXPECT_FALSE(programs.validate(overrides));
        EXPECT_EQ(programs.published(program_handle, "pbr"), nullptr);
        source = std::make_shared<ShaderProgramArtifact>();
        source->handle = program_handle;
        source->vertex_words.assign(PBR_VERT.begin(), PBR_VERT.end());
        source->fragment_words.assign(PBR_FRAG.begin(), PBR_FRAG.end());
        ASSERT_TRUE(assets.replace_asset(program_handle, source));
        EXPECT_TRUE(programs.validate(overrides));
        EXPECT_EQ(programs.published(program_handle, "pbr"), nullptr);
        overrides.scalar_properties["roughness"] = 2;
        EXPECT_FALSE(programs.validate(overrides));
        EXPECT_EQ(programs.published(program_handle, "pbr"), nullptr);
        overrides.scalar_properties.clear();
        programs.publish(program_handle, "pbr", source, MaterialLayout::find_builtin("pbr"));
        EXPECT_TRUE(programs.validate(overrides));
        auto next = std::make_shared<ShaderProgramArtifact>();
        next->handle = program_handle;
        ASSERT_TRUE(assets.replace_asset(program_handle, next));
        EXPECT_TRUE(programs.validate(overrides));
        const auto candidate = MaterialLayout::create("pbr", {}, 16, {{"custom", 0, 1}});
        ASSERT_TRUE(candidate);
        programs.publish(
            program_handle, "pbr", next, std::make_shared<MaterialLayout>(candidate.value()));
        EXPECT_FALSE(programs.validate(overrides));
        overrides.vector_properties.clear();
        overrides.scalar_properties["custom"] = 2;
        EXPECT_TRUE(programs.validate(overrides));
        ASSERT_TRUE(assets.unregister_asset(material_handle));
        EXPECT_FALSE(programs.validate(overrides));
    }

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
        const auto textured = MaterialLayout::find_builtin("pbr");
        ASSERT_TRUE(textured);
        EXPECT_EQ(textured, MaterialLayout::find_builtin("pbr"));
        EXPECT_EQ(textured->get_scalars().front().display_name, "Metallic");
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
