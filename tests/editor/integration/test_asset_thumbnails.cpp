#include "asset/asset_manager.h"
#include "support/render_graph_gpu_fixture.h"
#include "assets/asset_thumbnails.h"
#include "render/scene/scene_preview.h"
#include "asset/serialization/material_serializer.h"
#include "core/task_scheduler.h"

#include <thread>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, RenderedThumbnailReadsPixelsAndKeepsSubmittedResourcesAlive) {
        auto& renderer = engine->get_renderer();
        ScenePreview thumbnails(renderer, Math::Vec2u(CometEditor::AssetThumbnails::IMAGE_SIZE));
        auto world = std::make_shared<RenderWorld>();
        auto red = std::make_shared<Material>("preview", "unlit_color");
        ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
        world->render_items = {{.mesh = lit_quad(),
            .material = {.material_handle = AssetHandle(801), .resource = red->snapshot()}}};
        const RenderCamera camera{.view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
            .projection = RenderCamera::Projection::Orthographic,
            .orthographic_height = 2,
            .near_clip = 0.1f,
            .far_clip = 10};
        ASSERT_TRUE(thumbnails.submit(world, camera));
        EXPECT_FALSE(thumbnails.submit(world, camera));
        world.reset();
        red.reset();
        std::optional<TextureData> image;
        for(int i = 0; i < 3000 && !image; ++i) {
            auto polled = thumbnails.poll();
            ASSERT_TRUE(polled) << polled.error();
            image = std::move(polled).value();
            if(!image)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(image);
        EXPECT_EQ(image->width, CometEditor::AssetThumbnails::IMAGE_SIZE);
        EXPECT_EQ(image->height, image->width);
        EXPECT_EQ(image->format, Format::R8G8B8A8_SRGB);
        const auto offset = (image->height / 2 * image->width + image->width / 2) * 4;
        EXPECT_NEAR(image->pixels[offset], mapped_byte(1.0f), 2);
        EXPECT_LE(image->pixels[offset + 1], 1);
        EXPECT_LE(image->pixels[offset + 2], 1);
        EXPECT_EQ(image->pixels[offset + 3], 255);
        auto empty = thumbnails.poll();
        ASSERT_TRUE(empty);
        EXPECT_FALSE(empty.value());

        // 关闭面板时仍有 GPU 任务；析构等待自己的提交，不依赖主视口推进。
        ASSERT_TRUE(thumbnails.submit(std::make_shared<RenderWorld>(), camera));
    }

    TEST_F(RenderGraphGpuTest, MaterialAndModelThumbnailsAreCachedAndRefreshWithPublishedContent) {
        TemporaryDirectory directory;
        const ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        const auto path = paths.assets() / "color.mat";
        MaterialData material{
            .template_name = "unlit_color", .vector_properties = {{"color", {1, 0, 0, 1}}}};
        ASSERT_TRUE(MaterialSerializer{}.save(material, path));
        std::filesystem::copy_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "demo/assets/models/milk_truck.glb",
            paths.assets() / "truck.glb");
        TaskScheduler scheduler(1);
        AssetDatabase database(paths);
        AssetManager assets(database, engine->get_asset_registry(),
            engine->get_render_resource_factory(), scheduler);
        ASSERT_TRUE(assets.scan().succeeded());
        const auto* color = assets.get_database().find("color.mat");
        ASSERT_NE(color, nullptr);
        const auto material_handle = color->handle;
        const auto* model = assets.get_database().find("truck.glb");
        ASSERT_NE(model, nullptr);
        const auto model_handle = model->handle;
        ASSERT_TRUE(assets.import_model(model_handle));
        CometEditor::AssetThumbnails thumbnails(database, scheduler);
        thumbnails.enable_rendered_previews(
            engine->get_renderer(), engine->get_render_resource_factory());
        const auto runtime_assets = engine->get_asset_registry().size();
        const auto load = [&](AssetHandle handle) {
            CometEditor::AssetThumbnails::Preview preview;
            for(int i = 0; i < 3000; ++i) {
                thumbnails.update();
                preview = thumbnails.request(handle);
                if(preview.image || !preview.error.empty())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return preview;
        };
        auto red = load(material_handle);
        ASSERT_TRUE(red.image) << red.error;
        EXPECT_EQ(thumbnails.request(material_handle).image, red.image);
        auto truck = load(model_handle);
        ASSERT_TRUE(truck.image) << truck.error;
        EXPECT_EQ(engine->get_asset_registry().size(), runtime_assets);
        EXPECT_NE(truck.image->pixels, red.image->pixels);
        EXPECT_GT(std::ranges::count_if(
                      truck.image->pixels, [](uint8_t pixel) { return pixel > 10 && pixel < 250; }),
            100);

        thumbnails.invalidate(material_handle);
        EXPECT_FALSE(thumbnails.request(material_handle).image);
        thumbnails.update(); // 红色候选已经提交，编辑先于读回完成。
        material.vector_properties["color"] = {0, 0, 1, 1};
        ASSERT_TRUE(MaterialSerializer{}.save(material, path));
        ASSERT_TRUE(assets.scan().succeeded());
        ASSERT_TRUE(assets.reload_material(material_handle));
        auto blue = load(material_handle);
        ASSERT_TRUE(blue.image) << blue.error;
        EXPECT_NE(blue.image, red.image);
        EXPECT_NE(blue.image->pixels, red.image->pixels);
        auto channel_total = [](const TextureData& image, size_t channel) {
            uint64_t sum = 0;
            for(size_t i = channel; i < image.pixels.size(); i += 4)
                sum += image.pixels[i];
            return sum;
        };
        EXPECT_GT(channel_total(*red.image, 0), channel_total(*red.image, 2) + 10000);
        EXPECT_GT(channel_total(*blue.image, 2), channel_total(*blue.image, 0) + 10000);

        const auto shader_path = directory.path() / "dim.frag";
        ASSERT_TRUE(write_text_file_atomic(shader_path,
            "#version 450\nlayout(location=0) out vec4 color;\n"
            "layout(set=1,binding=0,std140) uniform MaterialData {vec4 color; float intensity;} material;\n"
            "void main(){color=vec4(material.color.rgb*material.intensity*0.25,material.color.a);}"));
        auto compiled =
            ShaderCompiler::compile({.source = shader_path, .stage = ShaderStage::Fragment});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        auto shader = default_material_shaders().at("unlit_color");
        shader.fragment = compiled.words;
        ASSERT_TRUE(engine->get_renderer().reload_material_shaders({{"unlit_color", shader}}));
        auto dim = load(material_handle);
        ASSERT_TRUE(dim.image) << dim.error;
        EXPECT_NE(dim.image, blue.image);
        EXPECT_LT(channel_total(*dim.image, 2), channel_total(*blue.image, 2) * 0.8);

        const std::array failed{std::string("unlit_color")};
        ASSERT_TRUE(engine->get_renderer().invalidate_material_shaders(failed));
        auto invalid = load(material_handle);
        ASSERT_TRUE(invalid.image) << invalid.error;
        EXPECT_NE(invalid.image->pixels, dim.image->pixels);
        ASSERT_TRUE(engine->get_renderer().reload_material_shaders({{"unlit_color", shader}}));
        auto restored = load(material_handle);
        ASSERT_TRUE(restored.image) << restored.error;
        EXPECT_EQ(restored.image->pixels, dim.image->pixels);

        // 同 revision 的强制重导入也必须重新生成，不能只依赖数据库变化。
        ASSERT_TRUE(assets.import_model(model_handle));
        thumbnails.invalidate(model_handle);
        auto reimported = load(model_handle);
        ASSERT_TRUE(reimported.image) << reimported.error;
        EXPECT_NE(reimported.image, truck.image);
        EXPECT_EQ(reimported.image->pixels, truck.image->pixels);
    }
}
