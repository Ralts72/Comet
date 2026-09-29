#include "render/material/material.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "render/material/material_runtime.h"
#include "scene/material_parameters.h"
#include "graphics/pipeline/shader_interface.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "pbr_vert.h"
#include "pbr_frag.h"

#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace Comet::Tests {
    TEST(MaterialRuntimeTest, PbrLayoutMatchesShaderAndPreservesOldParameterSnapshots) {
        const auto layout = MaterialLayout::find_builtin("pbr");
        ASSERT_TRUE(layout);
        const auto shader = ShaderInterface::reflect(PBR_FRAG);
        ASSERT_TRUE(shader) << shader.error();
        EXPECT_TRUE(layout->validate(shader.value()));
        const auto reflected = MaterialLayout::reflect(layout, shader.value());
        ASSERT_TRUE(reflected) << reflected.error();
        EXPECT_EQ(reflected.value(), layout);
        ASSERT_EQ(layout->get_parameter_size(), 32);
        ASSERT_EQ(layout->get_scalars().size(), 2);
        ASSERT_EQ(layout->get_textures().size(), 1);
        EXPECT_EQ(layout->get_textures()[0].name, "base_color_texture");
        EXPECT_TRUE(layout->get_textures()[0].optional);
        EXPECT_FLOAT_EQ(layout->get_scalars()[1].min_value, 0.045f);
        MaterialRuntimeCache cache;
        auto material = std::make_shared<Material>("pbr", "pbr");
        const auto original = cache.prepare(AssetHandle(782), material, layout);
        ASSERT_TRUE(original);
        ASSERT_EQ(original.value()->textures.size(), 1);
        EXPECT_EQ(original.value()->textures[0].binding, 1);
        EXPECT_EQ(original.value()->textures[0].texture, nullptr);
        std::array<float, 8> values{};
        std::memcpy(values.data(), original.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.8f);
        EXPECT_FLOAT_EQ(values[4], 0);
        EXPECT_FLOAT_EQ(values[5], 0.5f);
        EXPECT_TRUE(material->set_scalar_property("metallic", 0.75f));
        EXPECT_TRUE(material->set_scalar_property("roughness", 0.2f));
        EXPECT_TRUE(material->set_vector_property("base_color", {0.3f, 0.1f, 0.05f, 1}));
        const auto next = cache.prepare(AssetHandle(782), material, layout);
        ASSERT_TRUE(next);
        EXPECT_NE(next.value(), original.value());
        std::memcpy(values.data(), next.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.3f);
        EXPECT_FLOAT_EQ(values[4], 0.75f);
        EXPECT_FLOAT_EQ(values[5], 0.2f);
        EXPECT_FLOAT_EQ(values[6], 0);
        EXPECT_FLOAT_EQ(values[7], 0);
        EXPECT_EQ(next.value(), cache.prepare(AssetHandle(782), material, layout).value());
        std::memcpy(values.data(), original.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[4], 0);
        EXPECT_FLOAT_EQ(values[5], 0.5f);
        const auto& bindings = shader.value().get_bindings();
        const auto frame = std::ranges::find_if(
            bindings, [](const auto& binding) { return binding.set == 0 && binding.binding == 0; });
        ASSERT_NE(frame, bindings.end());
        EXPECT_EQ(frame->block_size, 160);
        ASSERT_EQ(frame->members.size(), 6);
        EXPECT_EQ(frame->members[2].name, "camera_position");
        EXPECT_EQ(frame->members[2].offset, 128);
        EXPECT_EQ(frame->members[3].name, "orthographic");
        EXPECT_EQ(frame->members[3].offset, 140);
        EXPECT_EQ(frame->members[4].name, "view_direction");
        EXPECT_EQ(frame->members[4].offset, 144);
    }

    TEST(MaterialRuntimeTest, PacksDefaultsAndParametersWithoutChangingOldSnapshots) {
        MaterialRuntimeCache cache;
        auto layout_result =
            MaterialLayout::create("solid", std::vector<MaterialLayout::TextureProperty>{}, 32,
                std::vector<MaterialLayout::ScalarProperty>{{"intensity", 16, 1.0f}},
                std::vector<MaterialLayout::VectorProperty>{{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(layout_result) << layout_result.error();
        const auto layout = std::make_shared<MaterialLayout>(std::move(layout_result).value());
        const auto material = std::make_shared<Material>("solid", "solid");
        const auto original = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(original);
        ASSERT_EQ(original.value()->parameters.size(), 32u);
        std::array<float, 8> values;
        std::memcpy(values.data(), original.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 1);
        EXPECT_FLOAT_EQ(values[4], 1);
        EXPECT_FLOAT_EQ(values[7], 0);
        EXPECT_TRUE(material->set_scalar_property("intensity", 0.25f));
        const Math::Vec4 color(0.2f, 0.4f, 0.6f, 0.8f);
        EXPECT_TRUE(material->set_vector_property("color", color));
        const auto updated = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(updated);
        std::memcpy(values.data(), updated.value()->parameters.data(), sizeof(values));
        for(int component = 0; component < 4; ++component)
            EXPECT_FLOAT_EQ(values[component], color[component]);
        EXPECT_FLOAT_EQ(values[4], 0.25f);
        EXPECT_FLOAT_EQ(values[5], 0);
        EXPECT_FLOAT_EQ(values[6], 0);
        EXPECT_FLOAT_EQ(values[7], 0);
        EXPECT_EQ(updated.value(), cache.prepare(AssetHandle(1), material, layout).value());
        std::memcpy(values.data(), original.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[1], 1);
        EXPECT_FLOAT_EQ(values[4], 1);
    }

    TEST(MaterialRuntimeTest, ReusesSnapshotAndInvalidatesMaterialOrLayoutIdentity) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(71);
        auto material = std::make_shared<Material>("solid", "solid");
        auto layout_result =
            MaterialLayout::create("solid", std::vector<MaterialLayout::TextureProperty>{});
        ASSERT_TRUE(layout_result) << layout_result.error();
        auto layout = std::make_shared<MaterialLayout>(std::move(layout_result).value());
        const auto first = cache.prepare(handle, material, layout);
        ASSERT_TRUE(first);
        EXPECT_TRUE(first.value()->textures.empty());
        cache.collect_unused();
        EXPECT_EQ(first.value(), cache.prepare(handle, material, layout).value());

        material->set_texture_property("unused", nullptr);
        const auto changed = cache.prepare(handle, material, layout);
        EXPECT_NE(first.value(), changed.value());
        const auto revision = material->get_revision();
        material->set_texture_property("unused", nullptr);
        EXPECT_EQ(material->get_revision(), revision);
        EXPECT_EQ(changed.value(), cache.prepare(handle, material, layout).value());

        material = std::make_shared<Material>("replacement", "solid");
        const auto replaced = cache.prepare(handle, material, layout);
        EXPECT_NE(changed.value(), replaced.value());
        material = std::make_shared<Material>("same revision", "solid");
        const auto same_revision = cache.prepare(handle, material, layout);
        EXPECT_NE(replaced.value(), same_revision.value());
        auto replacement =
            MaterialLayout::create("solid", std::vector<MaterialLayout::TextureProperty>{});
        ASSERT_TRUE(replacement) << replacement.error();
        layout = std::make_shared<MaterialLayout>(std::move(replacement).value());
        const auto new_layout = cache.prepare(handle, material, layout);
        EXPECT_NE(same_revision.value(), new_layout.value());
        EXPECT_EQ(new_layout.value(), cache.prepare(handle, material, layout).value());
    }

    TEST(MaterialRuntimeTest, EvictsUnusedEntriesWithoutInvalidatingExternalSnapshots) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("solid", "solid");
        auto layout_result =
            MaterialLayout::create("solid", std::vector<MaterialLayout::TextureProperty>{});
        ASSERT_TRUE(layout_result) << layout_result.error();
        const auto layout = std::make_shared<MaterialLayout>(std::move(layout_result).value());
        const auto snapshot = cache.prepare(AssetHandle(1), material, layout);
        ASSERT_TRUE(snapshot);
        cache.collect_unused();
        cache.collect_unused();
        EXPECT_NE(snapshot.value(), cache.prepare(AssetHandle(1), material, layout).value());
        EXPECT_EQ(snapshot.value()->layout, layout);
    }

    TEST(MaterialRuntimeTest, RejectsMissingResourcesAndRecoversAfterLayoutReplacement) {
        MaterialRuntimeCache cache;
        const auto material = std::make_shared<Material>("test", "solid");
        auto wrong_result =
            MaterialLayout::create("other", std::vector<MaterialLayout::TextureProperty>{});
        ASSERT_TRUE(wrong_result) << wrong_result.error();
        const auto wrong = std::make_shared<MaterialLayout>(std::move(wrong_result).value());
        auto missing_result = MaterialLayout::create(
            "solid", std::vector<MaterialLayout::TextureProperty>{{"albedo", 4}});
        ASSERT_TRUE(missing_result) << missing_result.error();
        const auto missing = std::make_shared<MaterialLayout>(std::move(missing_result).value());
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, wrong));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), nullptr, missing));
        EXPECT_FALSE(cache.prepare(AssetHandle(1), material, nullptr));
        auto fixed_result =
            MaterialLayout::create("solid", std::vector<MaterialLayout::TextureProperty>{});
        ASSERT_TRUE(fixed_result) << fixed_result.error();
        const auto fixed = std::make_shared<MaterialLayout>(std::move(fixed_result).value());
        EXPECT_TRUE(cache.prepare(AssetHandle(1), material, fixed));
    }

    TEST(MaterialRuntimeTest, IsolatesRuntimeOverridesAndPreservesSharedMaterialAndOldSnapshots) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(81);
        const auto material = std::make_shared<Material>("shared", "unlit_color");
        const auto layout = MaterialLayout::find_builtin("unlit_color");
        ASSERT_TRUE(layout);
        ASSERT_TRUE(material->set_scalar_property("intensity", 0.4f));
        ASSERT_TRUE(material->set_vector_property("color", {0.1f, 0.2f, 0.3f, 1}));
        const auto revision = material->get_revision();
        const auto first_overrides =
            std::make_shared<MaterialOverrides>(MaterialOverrides{.instance_id = 1,
                .material = handle,
                .scalar_properties = {{"intensity", 0.25f}},
                .vector_properties = {{"color", {0.2f, 0.4f, 0.6f, 1}}}});
        const auto second_overrides =
            std::make_shared<MaterialOverrides>(MaterialOverrides{.instance_id = 2,
                .material = handle,
                .scalar_properties = {{"intensity", 0.75f}},
                .vector_properties = {{"color", {0.6f, 0.4f, 0.2f, 1}}}});
        const auto base = cache.prepare(handle, material, layout);
        const auto first = cache.prepare(handle, material, layout, first_overrides);
        const auto second = cache.prepare(handle, material, layout, second_overrides);
        ASSERT_TRUE(base);
        ASSERT_TRUE(first) << first.error();
        ASSERT_TRUE(second) << second.error();
        EXPECT_NE(first.value(), second.value());
        EXPECT_EQ(first.value(), cache.prepare(handle, material, layout, first_overrides).value());
        EXPECT_EQ(
            second.value(), cache.prepare(handle, material, layout, second_overrides).value());
        EXPECT_EQ(base.value(), cache.prepare(handle, material, layout).value());
        std::array<float, 8> values{};
        std::memcpy(values.data(), first.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.2f);
        EXPECT_FLOAT_EQ(values[4], 0.25f);
        std::memcpy(values.data(), second.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.6f);
        EXPECT_FLOAT_EQ(values[4], 0.75f);
        EXPECT_EQ(material->get_revision(), revision);
        EXPECT_FLOAT_EQ(*material->get_scalar_property("intensity"), 0.4f);
        EXPECT_EQ(*material->get_vector_property("color"), Math::Vec4(0.1f, 0.2f, 0.3f, 1));

        auto changed_overrides = std::make_shared<MaterialOverrides>(*first_overrides);
        changed_overrides->scalar_properties["intensity"] = 0.9f;
        const auto changed = cache.prepare(handle, material, layout, changed_overrides);
        ASSERT_TRUE(changed);
        EXPECT_NE(first.value(), changed.value());
        std::memcpy(values.data(), changed.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[4], 0.9f);
        std::memcpy(values.data(), first.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[4], 0.25f);

        const auto cleared_overrides = std::make_shared<MaterialOverrides>(
            MaterialOverrides{.instance_id = 1, .material = handle});
        const auto cleared = cache.prepare(handle, material, layout, cleared_overrides);
        ASSERT_TRUE(cleared);
        std::memcpy(values.data(), cleared.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.1f);
        EXPECT_FLOAT_EQ(values[4], 0.4f);
        EXPECT_EQ(
            second.value(), cache.prepare(handle, material, layout, second_overrides).value());
    }

    TEST(MaterialRuntimeTest, RebindsRuntimeOverridesAndRejectsIncompatibleLayoutTransaction) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(82);
        const auto material = std::make_shared<Material>("shared", "solid");
        auto original_layout = MaterialLayout::create(
            "solid", {}, 32, {{"intensity", 16, 1.0f}}, {{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(original_layout);
        const auto layout = std::make_shared<MaterialLayout>(std::move(original_layout).value());
        const auto overrides =
            std::make_shared<MaterialOverrides>(MaterialOverrides{.instance_id = 3,
                .material = handle,
                .scalar_properties = {{"intensity", 0.6f}},
                .vector_properties = {{"color", {0.2f, 0.3f, 0.4f, 1}}}});
        const auto original = cache.prepare(handle, material, layout, overrides);
        ASSERT_TRUE(original);
        auto shifted_layout = MaterialLayout::create(
            "solid", {}, 48, {{"intensity", 0, 1.0f}}, {{"color", 16, {1, 1, 1, 1}}});
        ASSERT_TRUE(shifted_layout);
        const auto shifted = std::make_shared<MaterialLayout>(std::move(shifted_layout).value());
        auto candidates = cache;
        const auto rebound = candidates.rebind(MaterialInstanceKey{handle, 3}, shifted);
        ASSERT_TRUE(rebound) << rebound.error();
        ASSERT_EQ(rebound.value()->parameters.size(), 48u);
        std::array<float, 12> values{};
        std::memcpy(values.data(), rebound.value()->parameters.data(), sizeof(values));
        EXPECT_FLOAT_EQ(values[0], 0.6f);
        EXPECT_FLOAT_EQ(values[4], 0.2f);
        EXPECT_FLOAT_EQ(values[5], 0.3f);
        EXPECT_FLOAT_EQ(values[6], 0.4f);
        EXPECT_EQ(original.value(), cache.prepare(handle, material, layout, overrides).value());

        auto incompatible_layout =
            MaterialLayout::create("solid", {}, 32, {}, {{"color", 0, {1, 1, 1, 1}}});
        ASSERT_TRUE(incompatible_layout);
        const auto incompatible =
            std::make_shared<MaterialLayout>(std::move(incompatible_layout).value());
        EXPECT_FALSE(candidates.rebind(MaterialInstanceKey{handle, 3}, incompatible));
        EXPECT_EQ(original.value(), cache.prepare(handle, material, layout, overrides).value());
        std::array<float, 8> old_values{};
        std::memcpy(old_values.data(), original.value()->parameters.data(), sizeof(old_values));
        EXPECT_FLOAT_EQ(old_values[0], 0.2f);
        EXPECT_FLOAT_EQ(old_values[4], 0.6f);

        ASSERT_TRUE(material->set_vector_property("color", {0, 1, 0, 1}));
        const auto revised = cache.prepare(handle, material, layout, overrides);
        ASSERT_TRUE(revised);
        EXPECT_NE(original.value(), revised.value());
        std::memcpy(old_values.data(), revised.value()->parameters.data(), sizeof(old_values));
        EXPECT_FLOAT_EQ(old_values[0], 0.2f);
        auto replacement = std::make_shared<Material>("replacement", "solid");
        const auto replaced = cache.prepare(handle, replacement, layout, overrides);
        ASSERT_TRUE(replaced);
        EXPECT_NE(revised.value(), replaced.value());
    }

    TEST(MaterialRuntimeTest, CollectsRuntimeInstancesAndRejectsMismatchedIdentity) {
        MaterialRuntimeCache cache;
        const AssetHandle handle(83);
        const auto material = std::make_shared<Material>("shared", "unlit_color");
        const auto layout = MaterialLayout::find_builtin("unlit_color");
        auto overrides = std::make_shared<MaterialOverrides>(MaterialOverrides{
            .instance_id = 4, .material = handle, .scalar_properties = {{"intensity", 0.5f}}});
        const auto old = cache.prepare(handle, material, layout, overrides);
        ASSERT_TRUE(old);
        cache.collect_unused();
        cache.collect_unused();
        const auto current = cache.prepare(handle, material, layout, overrides);
        ASSERT_TRUE(current);
        EXPECT_NE(old.value(), current.value());
        const auto base = cache.prepare(handle, material, layout);
        ASSERT_TRUE(base);
        cache.erase(MaterialInstanceKey{handle, 4});
        EXPECT_FALSE(cache.rebind(MaterialInstanceKey{handle, 4}, layout));
        EXPECT_EQ(base.value(), cache.prepare(handle, material, layout).value());
        ASSERT_TRUE(cache.prepare(handle, material, layout, overrides));
        cache.erase(handle);
        EXPECT_FALSE(cache.rebind(MaterialInstanceKey{handle, 4}, layout));
        EXPECT_FALSE(cache.rebind(handle, layout));
        EXPECT_EQ(old.value()->layout, layout);
        EXPECT_FALSE(cache.prepare(AssetHandle(84), material, layout, overrides));
        const auto invalid =
            std::make_shared<MaterialOverrides>(MaterialOverrides{.material = handle});
        EXPECT_FALSE(cache.prepare(handle, material, layout, invalid));
    }

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
