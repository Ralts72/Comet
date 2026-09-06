#include "asset/asset_manager.h"
#include "common/file_io.h"
#include "core/engine.h"
#include "diagnostics/diagnostics.h"
#include "scene/scene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
    constexpr unsigned WARMUP_FRAMES = 32;

    unsigned parse_number(const char* argument, unsigned minimum, unsigned maximum) {
        const std::string_view text(argument);
        unsigned value = 0;
        const auto [end, error] =
            std::from_chars(text.data(), text.data() + text.size(), value);
        if(error != std::errc{} || end != text.data() + text.size() || value < minimum
            || value > maximum)
            throw std::invalid_argument("Invalid profile argument: " + std::string(text));
        return value;
    }

    // 只清理由本次进程成功创建的目录，不修改源项目或复用其导入缓存。
    struct ProfileProject {
        std::filesystem::path root;
        ProfileProject() {
            auto candidate =
                std::filesystem::temp_directory_path()
                / ("comet-render-profile-"
                    + std::to_string(Comet::AssetHandle::generate().value()));
            if(!std::filesystem::create_directory(candidate))
                throw std::runtime_error("Cannot create isolated profile project");
            root = std::move(candidate);
        }
        ~ProfileProject() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
            if(error)
                std::cerr << "Profile cleanup failed: " << error.message() << '\n';
        }
    };

    void prepare_project(const ProfileProject& project) {
        for(const auto* relative : {"meshes/cube.gltf", "meshes/cube.gltf.meta",
                "materials/pbr.mat", "materials/pbr.mat.meta"}) {
            const auto destination = project.root / "assets" / relative;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(
                std::filesystem::path(PROJECT_ROOT_DIR) / "assets" / relative,
                destination);
        }
    }

    void populate_scene(
        Comet::Engine& engine, Comet::AssetManager& assets, const unsigned object_count) {
        if(!assets.scan().succeeded())
            throw std::runtime_error("Profile asset scan failed");
        const auto* mesh = assets.get_database().find("meshes/cube.gltf");
        const auto* material = assets.get_database().find("materials/pbr.mat");
        if(!mesh || !material || !assets.import_mesh(mesh->handle)
            || !assets.load_mesh(mesh->handle) || !assets.load_material(material->handle))
            throw std::runtime_error("Profile assets failed to import or load");
        auto scene = std::make_unique<Comet::Scene>();
        auto camera = scene->create_entity("Camera");
        camera.add_component<Comet::CameraComponent>().primary = true;
        auto& camera_transform = camera.get_component<Comet::TransformComponent>();
        camera_transform.translation = {0, 10, 16};
        camera_transform.rotation.x = -30;
        const auto columns = static_cast<unsigned>(std::ceil(std::sqrt(object_count)));
        const float spacing = 10.0f / columns;
        for(unsigned index = 0; index < object_count; ++index) {
            auto entity = scene->create_entity("PBR cube");
            entity.add_component<Comet::MeshRendererComponent>(
                mesh->handle, material->handle);
            auto& transform = entity.get_component<Comet::TransformComponent>();
            transform.translation = {(index % columns + 0.5f) * spacing - 5, 0,
                (index / columns + 0.5f) * spacing - 5};
            transform.rotation.y = 20;
            transform.scale = Comet::Math::Vec3(spacing * 0.65f);
        }
        auto ground = scene->create_entity("Ground");
        ground.add_component<Comet::MeshRendererComponent>(
            mesh->handle, material->handle);
        auto& ground_transform = ground.get_component<Comet::TransformComponent>();
        ground_transform.translation.y = -spacing * 0.325f - 0.1f;
        ground_transform.scale = {12, 0.2f, 12};
        auto key = scene->create_entity("Directional");
        key.get_component<Comet::TransformComponent>().rotation = {-30, -35, 0};
        auto& directional = key.add_component<Comet::LightComponent>();
        directional.intensity = 4;
        directional.casts_shadow = true;
        auto point = scene->create_entity("Point");
        point.get_component<Comet::TransformComponent>().translation = {-3, 2, 0};
        auto& point_light = point.add_component<Comet::LightComponent>();
        point_light.type = Comet::LightType::Point;
        point_light.color = {1, 0.2f, 0.1f};
        point_light.intensity = 20;
        point_light.range = 8;
        auto spot = scene->create_entity("Spot");
        auto& spot_transform = spot.get_component<Comet::TransformComponent>();
        spot_transform.translation = {3, 4, 3};
        spot_transform.rotation = {-50, 30, 0};
        auto& spot_light = spot.add_component<Comet::LightComponent>();
        spot_light.type = Comet::LightType::Spot;
        spot_light.color = {0.1f, 0.2f, 1};
        spot_light.intensity = 20;
        spot_light.range = 12;
        engine.set_scene(std::move(scene));
    }

    void measure(const std::filesystem::path& output, unsigned objects, unsigned width,
        unsigned height, unsigned sample_count, bool bloom) {
        ProfileProject project;
        prepare_project(project);
        Comet::Config config;
        config.window.width = static_cast<int>(width);
        config.window.height = static_cast<int>(height);
        config.window.resizable = false;
        config.window.title = "Comet Forward Profile";
        config.vulkan.msaa_samples = Comet::SampleCount::Count4;
        config.vulkan.enable_validation = false;
        config.diagnostics.log.level = "warn";
        config.diagnostics.log.enable_file_logging = false;
        config.diagnostics.enable_render_diagnostics = true;
        config.render.bloom_strength = bloom ? 0.15f : 0;
        Comet::Diagnostics logging(config.diagnostics);
        Comet::Engine engine(config);
        Comet::AssetManager assets(Comet::ProjectPaths(project.root),
            engine.get_asset_registry(), engine.get_resource_manager(),
            engine.get_task_scheduler());
        populate_scene(engine, assets, objects);
        auto& renderer = engine.get_renderer();
        auto& scene_renderer = renderer.get_scene_renderer();
        auto& diagnostics = scene_renderer.get_diagnostics();
        const auto initial_size = scene_renderer.get_render_target().get_size();
        const auto last = WARMUP_FRAMES + sample_count;
        const auto in_range = [last](uint64_t serial) {
            return serial > WARMUP_FRAMES && serial <= last;
        };
        std::map<std::string, std::vector<double>> measurements;
        unsigned skipped = 0;
        uint64_t last_gpu = 0;
        const auto append_graph =
            [&](const char* kind, const Comet::RenderDiagnostics::FrameTiming& frame) {
                measurements[std::string(kind) + "_graph"].push_back(frame.milliseconds);
                for(const auto& pass : frame.passes)
                    measurements[std::string(kind) + "_" + pass.name].push_back(
                        pass.milliseconds);
            };
        engine.register_update_callback([&](Comet::UpdateContext update) {
            if(update.frame_index == static_cast<int>(WARMUP_FRAMES)) {
                for(const auto* name : {"cpu_wall", "cpu_events", "cpu_update",
                        "cpu_prepare", "cpu_render_submit", "cpu_graph", "gpu_graph"})
                    measurements[name].reserve(sample_count);
                if(const auto& frame = diagnostics.get_snapshot().cpu)
                    for(const auto& pass : frame->passes)
                        for(const auto* kind : {"cpu_", "gpu_"})
                            measurements[std::string(kind) + pass.name].reserve(
                                sample_count);
            }
            if(const auto& frame = engine.get_frame_timing();
                frame && in_range(frame->frame_index)) {
                skipped += !frame->rendered;
                measurements["cpu_wall"].push_back(frame->total_ms);
                measurements["cpu_events"].push_back(frame->events_ms);
                measurements["cpu_update"].push_back(frame->update_ms);
                measurements["cpu_prepare"].push_back(frame->prepare_ms);
                measurements["cpu_render_submit"].push_back(frame->render_submit_ms);
            }
            const auto& snapshot = diagnostics.get_snapshot();
            if(snapshot.cpu && in_range(snapshot.cpu->serial))
                append_graph("cpu", *snapshot.cpu);
            if(snapshot.gpu && snapshot.gpu->serial != last_gpu) {
                last_gpu = snapshot.gpu->serial;
                if(in_range(last_gpu))
                    append_graph("gpu", *snapshot.gpu);
            }
            // 多运行三个循环让延迟查询自然可见，不为逐帧采样等待 GPU。
            if(update.frame_index >= static_cast<int>(last + 3))
                glfwSetWindowShouldClose(engine.get_window().get(), GLFW_TRUE);
        });
        engine.on_update();
        if(skipped || measurements["cpu_wall"].size() != sample_count
            || measurements["cpu_graph"].size() != sample_count
            || scene_renderer.get_render_target().get_size() != initial_size)
            throw std::runtime_error(
                "Profile interrupted by window or presentation changes");
        const auto& stats = scene_renderer.get_material_statistics();
        if(stats.draw_calls != objects + 1 || stats.light_count != 3
            || stats.pipeline_binds != 1 || stats.material_binds != 1
            || stats.cached_material_versions != 1 || !diagnostics.get_snapshot().cpu
            || diagnostics.get_snapshot().cpu->passes.size() != (bloom ? 6u : 3u))
            throw std::runtime_error(
                "Profile did not execute the expected forward scene");
        auto& device = renderer.get_render_context().get_device();
        const auto properties = device.get_capability().physical_device.getProperties();
        const auto& swapchain = renderer.get_render_context()
                                    .get_swapchain()
                                    .get_active_generation()
                                    ->get_config();
        std::ostringstream report;
        report << "# build=" << COMET_PROFILE_BUILD_TYPE << " validation_request=off\n"
               << "# device=" << properties.deviceName.data()
               << " driver=" << properties.driverVersion << '\n'
               << "# objects=" << objects << " draws=" << stats.draw_calls
               << " lights=" << stats.light_count << " msaa=4 bloom=" << bloom << '\n'
               << "# pipeline_binds=" << stats.pipeline_binds
               << " material_binds=" << stats.material_binds
               << " cached_material_versions=" << stats.cached_material_versions << '\n'
               << "# window=" << width << 'x' << height
               << " framebuffer=" << initial_size.x << 'x' << initial_size.y
               << " present_mode=" << vk::to_string(swapchain.present_mode) << '\n'
               << "# warmup=" << WARMUP_FRAMES << " requested_samples=" << sample_count
               << " gpu_supported=" << diagnostics.get_snapshot().gpu_supported << '\n'
               << "# gpu_error=" << diagnostics.get_snapshot().gpu_error << '\n';
        uint64_t allocated = 0;
        for(const auto& heap : device.query_memory_budget().heaps)
            allocated += heap.allocation_bytes;
        report << "# vma_allocation_bytes=" << allocated << '\n'
               << "metric,samples,p50_ms,p95_ms\n";
        for(auto& [name, values] : measurements) {
            if(values.empty())
                continue;
            std::sort(values.begin(), values.end());
            const auto percentile = [&](double p) {
                return values[static_cast<size_t>(std::ceil(values.size() * p)) - 1];
            };
            report << name << ',' << values.size() << ',' << percentile(0.5) << ','
                   << percentile(0.95) << '\n';
        }
        Comet::write_text_file_atomic(output, report.str());
        std::cout << report.str();
    }
}

int main(int argc, char** argv) {
    if(argc != 7) {
        std::cerr
            << "Usage: render_profile OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1)\n";
        return 2;
    }
    try {
        measure(argv[1], parse_number(argv[2], 1, 4096), parse_number(argv[3], 64, 4096),
            parse_number(argv[4], 64, 4096), parse_number(argv[5], 8, 10000),
            parse_number(argv[6], 0, 1) != 0);
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
