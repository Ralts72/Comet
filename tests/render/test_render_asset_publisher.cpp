#include "asset/data/environment_data.h"
#include "asset/data/material_data.h"
#include "asset/registry.h"
#include "asset/runtime/render_asset_publisher.h"
#include "graphics/error.h"
#include "render/material/material.h"
#include "render/resource/environment.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "support/render_resource_factory.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    class RenderAssetPublisherTest: public ::testing::Test {
    protected:
        static constexpr AssetHandle handle{42};
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        RenderAssetPublisher publisher{registry, factory};
    };

    TEST_F(RenderAssetPublisherTest, PreparationAndPublicationShareTheOnlyRegistry) {
        auto original = publisher.prepare(MeshData{});
        auto next = publisher.prepare(MeshData{});
        ASSERT_TRUE(original);
        ASSERT_TRUE(next);
        EXPECT_EQ(registry.size(), 0u);
        EXPECT_FALSE(publisher.publish(handle, original.value(), true));
        ASSERT_TRUE(publisher.publish(handle, original.value(), false));
        EXPECT_EQ(publisher.mesh(handle), registry.resolve<Mesh>(handle));
        EXPECT_FALSE(publisher.publish(handle, next.value(), false));
        EXPECT_EQ(publisher.mesh(handle), original.value());
        ASSERT_TRUE(publisher.publish(handle, next.value(), true));
        EXPECT_EQ(registry.resolve<Mesh>(handle), next.value());
        EXPECT_NE(original.value(), next.value());

        auto texture = publisher.prepare(TextureData{});
        ASSERT_TRUE(texture);
        EXPECT_FALSE(publisher.publish(handle, texture.value(), true));
        EXPECT_FALSE(publisher.texture(handle));
        EXPECT_EQ(registry.resolve<Mesh>(handle), next.value());
        EXPECT_EQ(registry.size(), 1u);
    }

    TEST_F(RenderAssetPublisherTest, PartialEnvironmentFailureKeepsPublishedVersionAndErrorCode) {
        auto previous = publisher.prepare(EnvironmentData{});
        ASSERT_TRUE(previous);
        ASSERT_TRUE(publisher.publish(handle, previous.value(), false));
        const std::weak_ptr<Texture> old_background = previous.value()->background;
        factory.on_next_texture_creation([&] {
            // Fail after the first successful upload, rather than before creating any object.
            factory.on_next_texture_creation([&] { factory.fail_texture_creation(true); });
        });
        auto failed = publisher.prepare(EnvironmentData{});
        ASSERT_FALSE(failed);
        EXPECT_FALSE(is_device_lost(failed.error()));
        EXPECT_EQ(failed.error().code,
            (GraphicsError{"", vk::Result::eErrorOutOfDeviceMemory}.as_error().code));
        EXPECT_EQ(publisher.environment(handle), previous.value());
        EXPECT_FALSE(old_background.expired());
        EXPECT_TRUE(publisher.has_lighting(previous.value()));
        EXPECT_EQ(factory.texture_creation_count(), 6u);

        factory.set_failure_result(vk::Result::eErrorDeviceLost);
        auto lost = publisher.prepare(TextureData{});
        ASSERT_FALSE(lost);
        EXPECT_TRUE(is_device_lost(lost.error()));
        EXPECT_EQ(publisher.environment(handle), previous.value());
    }

    TEST_F(RenderAssetPublisherTest, PreviewIsPublishedSeparatelyFromTheCompleteLightingSet) {
        auto preview = publisher.prepare_preview(TextureData{});
        ASSERT_TRUE(preview);
        EXPECT_FALSE(publisher.has_lighting(preview.value()));
        EXPECT_FALSE(publisher.has_lighting({}));
        EXPECT_FALSE(registry.contains(handle));
        ASSERT_TRUE(publisher.publish(handle, preview.value(), false));
        auto complete = publisher.prepare(EnvironmentData{});
        ASSERT_TRUE(complete);
        EXPECT_TRUE(publisher.has_lighting(complete.value()));
        EXPECT_EQ(publisher.environment(handle), preview.value());
        ASSERT_TRUE(publisher.publish(handle, complete.value(), true));
        EXPECT_EQ(publisher.environment(handle), complete.value());
        EXPECT_FALSE(publisher.has_lighting(preview.value()));
    }

    TEST_F(
        RenderAssetPublisherTest, MaterialUsesResolvedDependenciesWithoutLoadingOrUploadingThem) {
        auto texture = publisher.prepare(TextureData{});
        ASSERT_TRUE(texture);
        const MaterialData data{.template_name = "standard",
            .shader_program = AssetHandle{84},
            .texture_properties = {{"albedo", AssetHandle{168}}},
            .scalar_properties = {{"roughness", 0.5f}},
            .vector_properties = {{"tint", Math::Vec4{1.0f}}}};
        const std::map<std::string, std::shared_ptr<Texture>> dependencies{
            {"albedo", texture.value()}};
        auto material = publisher.prepare_material("demo", data, dependencies);
        ASSERT_TRUE(material);
        EXPECT_EQ(material.value()->get_texture_property("albedo"), texture.value());
        EXPECT_EQ(material.value()->get_scalar_property("roughness"), 0.5f);
        EXPECT_EQ(material.value()->get_vector_property("tint"), Math::Vec4{1.0f});
        EXPECT_EQ(material.value()->get_shader_program(), AssetHandle{84});
        EXPECT_EQ(factory.texture_creation_count(), 1u);
        EXPECT_EQ(registry.size(), 0u);
        ASSERT_TRUE(publisher.publish(handle, material.value(), false));
        EXPECT_EQ(publisher.material(handle), material.value());

        EXPECT_FALSE(publisher.prepare_material("missing", data, {}));
        auto invalid = data;
        invalid.scalar_properties["roughness"] = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(publisher.prepare_material("invalid", invalid, dependencies));
        invalid = data;
        invalid.vector_properties["tint"].x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(publisher.prepare_material("invalid", invalid, dependencies));
        EXPECT_EQ(publisher.material(handle), material.value());
    }
}
