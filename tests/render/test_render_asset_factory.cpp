#include "asset/data/environment_data.h"
#include "asset/data/material_data.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "asset/registry.h"
#include "asset/runtime/render_asset_factory.h"
#include "graphics/error.h"
#include "render/material/material.h"
#include "render/resource/environment.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "support/render_resource_factory.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    class RenderAssetFactoryTest: public ::testing::Test {
    protected:
        static constexpr AssetHandle handle{42};
        AssetRegistry registry;
        FakeRenderResourceFactory resources;
        RenderAssetFactory factory{resources};
    };

    TEST_F(RenderAssetFactoryTest, CreatesCandidatesBeforeOwnerRegistration) {
        auto original = factory.prepare(MeshData{});
        auto next = factory.prepare(MeshData{});
        ASSERT_TRUE(original);
        ASSERT_TRUE(next);
        EXPECT_EQ(registry.size(), 0u);
        EXPECT_FALSE(registry.replace_asset(handle, original.value()));
        ASSERT_TRUE(registry.register_asset(handle, original.value()));
        EXPECT_FALSE(registry.register_asset(handle, next.value()));
        EXPECT_EQ(registry.resolve<Mesh>(handle), original.value());
        ASSERT_TRUE(registry.replace_asset(handle, next.value()));
        EXPECT_EQ(registry.resolve<Mesh>(handle), next.value());
        EXPECT_NE(original.value(), next.value());

        auto texture = factory.prepare(TextureData{});
        ASSERT_TRUE(texture);
        EXPECT_FALSE(registry.replace_asset(handle, texture.value()));
        EXPECT_FALSE(registry.resolve<Texture>(handle));
        EXPECT_EQ(registry.resolve<Mesh>(handle), next.value());
        EXPECT_EQ(registry.size(), 1u);
    }

    TEST_F(RenderAssetFactoryTest, PartialEnvironmentFailureKeepsPublishedVersionAndErrorCode) {
        auto previous = factory.prepare(EnvironmentData{});
        ASSERT_TRUE(previous);
        ASSERT_TRUE(registry.register_asset(handle, previous.value()));
        const std::weak_ptr<Texture> old_background = previous.value()->background;
        resources.on_next_texture_creation([&] {
            // Fail after the first successful upload, rather than before creating any object.
            resources.on_next_texture_creation([&] { resources.fail_texture_creation(true); });
        });
        auto failed = factory.prepare(EnvironmentData{});
        ASSERT_FALSE(failed);
        EXPECT_FALSE(is_device_lost(failed.error()));
        EXPECT_EQ(failed.error().code,
            (GraphicsError{"", vk::Result::eErrorOutOfDeviceMemory}.as_error().code));
        EXPECT_EQ(registry.resolve<Environment>(handle), previous.value());
        EXPECT_FALSE(old_background.expired());
        EXPECT_TRUE(RenderAssetFactory::has_lighting(previous.value()));
        EXPECT_EQ(resources.texture_creation_count(), 6u);

        resources.set_failure_result(vk::Result::eErrorDeviceLost);
        auto lost = factory.prepare(TextureData{});
        ASSERT_FALSE(lost);
        EXPECT_TRUE(is_device_lost(lost.error()));
        EXPECT_EQ(registry.resolve<Environment>(handle), previous.value());
    }

    TEST_F(RenderAssetFactoryTest, PreviewIsPublishedSeparatelyFromTheCompleteLightingSet) {
        auto preview = factory.prepare_preview(TextureData{});
        ASSERT_TRUE(preview);
        EXPECT_FALSE(RenderAssetFactory::has_lighting(preview.value()));
        EXPECT_FALSE(RenderAssetFactory::has_lighting({}));
        EXPECT_FALSE(registry.contains(handle));
        ASSERT_TRUE(registry.register_asset(handle, preview.value()));
        auto complete = factory.prepare(EnvironmentData{});
        ASSERT_TRUE(complete);
        EXPECT_TRUE(RenderAssetFactory::has_lighting(complete.value()));
        EXPECT_EQ(registry.resolve<Environment>(handle), preview.value());
        ASSERT_TRUE(registry.replace_asset(handle, complete.value()));
        EXPECT_EQ(registry.resolve<Environment>(handle), complete.value());
        EXPECT_FALSE(RenderAssetFactory::has_lighting(preview.value()));
    }

    TEST_F(RenderAssetFactoryTest, MaterialUsesResolvedDependenciesWithoutLoadingOrUploadingThem) {
        auto texture = factory.prepare(TextureData{});
        ASSERT_TRUE(texture);
        const MaterialData data{.template_name = "standard",
            .shader_program = AssetHandle{84},
            .texture_properties = {{"albedo", AssetHandle{168}}},
            .scalar_properties = {{"roughness", 0.5f}},
            .vector_properties = {{"tint", Math::Vec4{1.0f}}}};
        const std::map<std::string, std::shared_ptr<Texture>> dependencies{
            {"albedo", texture.value()}};
        auto material = RenderAssetFactory::prepare_material("demo", data, dependencies);
        ASSERT_TRUE(material);
        EXPECT_EQ(material.value()->get_texture_property("albedo"), texture.value());
        EXPECT_EQ(material.value()->get_scalar_property("roughness"), 0.5f);
        EXPECT_EQ(material.value()->get_vector_property("tint"), Math::Vec4{1.0f});
        EXPECT_EQ(material.value()->get_shader_program(), AssetHandle{84});
        EXPECT_EQ(resources.texture_creation_count(), 1u);
        EXPECT_EQ(registry.size(), 0u);
        ASSERT_TRUE(registry.register_asset(handle, material.value()));
        EXPECT_EQ(registry.resolve<Material>(handle), material.value());

        EXPECT_FALSE(RenderAssetFactory::prepare_material("missing", data, {}));
        auto invalid = data;
        invalid.scalar_properties["roughness"] = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(RenderAssetFactory::prepare_material("invalid", invalid, dependencies));
        invalid = data;
        invalid.vector_properties["tint"].x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(RenderAssetFactory::prepare_material("invalid", invalid, dependencies));
        EXPECT_EQ(registry.resolve<Material>(handle), material.value());
    }
}
