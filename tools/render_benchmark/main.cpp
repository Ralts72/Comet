#include "asset/asset_manager.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "core/engine.h"
#include "core/window.h"
#include "diagnostics/diagnostics.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "render/render_context.h"
#include "render/render_diagnostics.h"
#include "render/renderer.h"
#include "render/resource/render_resources.h"
#include "render/scene/scene_renderer.h"
#include "render/render_target.h"
#include "scene/scene.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
    using Comet::Result;
    constexpr unsigned WARMUP_FRAMES = 32;
    constexpr std::string_view USAGE =
        "Usage: render_benchmark OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1)";

    struct Options {
        std::filesystem::path output;
        unsigned objects, width, height, frames;
        bool bloom;
    };

    Result<Options> parse_options(int argc, char** argv) {
#ifdef __APPLE__
        if(argc == 9 && std::string_view(argv[7]) == "-NSAutomaticWindowAnimationsEnabled"
            && std::string_view(argv[8]) == "NO")
            argc = 7;
#endif
        if(argc != 7 || std::string_view(argv[1]).empty())
            return Result<Options>::failure(std::string(USAGE));
        const std::array<unsigned, 5> minimum{1, 64, 64, 8, 0};
        const std::array<unsigned, 5> maximum{4096, 4096, 4096, 10000, 1};
        std::array<unsigned, 5> values{};
        for(size_t index = 0; index < values.size(); ++index) {
            const std::string_view text(argv[index + 2]);
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), values[index]);
            if(error != std::errc{} || end != text.data() + text.size()
                || values[index] < minimum[index] || values[index] > maximum[index])
                return Result<Options>::failure("Invalid benchmark argument: " + std::string(text));
        }
        return Result<Options>::success(
            {argv[1], values[0], values[1], values[2], values[3], values[4] != 0});
    }

    Result<std::filesystem::path> create_temporary_project() {
        std::error_code error;
        auto parent = std::filesystem::temp_directory_path(error);
        if(!error)
            parent = std::filesystem::canonical(parent, error);
        if(error)
            return Result<std::filesystem::path>::failure(
                "Cannot locate temporary directory: " + error.message());
        std::random_device random;
        for(int attempt = 0; attempt < 32; ++attempt) {
            auto candidate = parent / ("comet_benchmark_" + std::to_string(random()));
            if(std::filesystem::create_directory(candidate, error))
                return Result<std::filesystem::path>::success(std::move(candidate));
            if(error && error != std::errc::file_exists)
                return Result<std::filesystem::path>::failure(
                    "Cannot create benchmark directory: " + error.message());
        }
        return Result<std::filesystem::path>::failure("Cannot create unique benchmark directory");
    }

    Result<void> prepare_assets(const std::filesystem::path& root) {
        for(const auto* relative : {"meshes/cube.gltf", "meshes/cube.gltf.meta",
                "materials/ground.mat", "materials/ground.mat.meta"}) {
            const auto destination = root / "assets" / relative;
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            if(!error)
                std::filesystem::copy_file(
                    std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets" / relative,
                    destination, error);
            if(error)
                return Result<void>::failure("Cannot copy benchmark asset: " + error.message());
        }
        return Result<void>::success();
    }

    Result<void> populate_scene(
        Comet::Engine& engine, Comet::AssetManager& assets, const Options& options) {
        if(!assets.scan().succeeded())
            return Result<void>::failure("Benchmark asset scan failed");
        const auto* mesh = assets.get_database().find("meshes/cube.gltf");
        const auto* material = assets.get_database().find("materials/ground.mat");
        if(!mesh || !material)
            return Result<void>::failure("Benchmark assets were not indexed");
        if(auto imported = assets.import_mesh(mesh->handle); !imported)
            return Result<void>::failure(imported.error().message);
        for(const auto* record : {mesh, material}) {
            if(auto loaded = assets.ensure_loaded(record->handle, record->type); !loaded)
                return Result<void>::failure(loaded.error().message);
        }
        auto scene = std::make_unique<Comet::Scene>();
        if(!scene->set_post_process({.bloom_enabled = options.bloom}))
            return Result<void>::failure("Invalid benchmark post processing settings");
        auto camera = scene->create_entity("Camera");
        camera.add_component<Comet::CameraComponent>().primary = true;
        camera.set_transform({.translation = {0, 10, 16}, .rotation = {-30, 0, 0}});
        const auto columns = static_cast<unsigned>(std::ceil(std::sqrt(options.objects)));
        const float spacing = 10.0f / columns;
        for(unsigned index = 0; index < options.objects; ++index) {
            auto entity = scene->create_entity("PBR cube");
            entity.add_component<Comet::MeshRendererComponent>(mesh->handle, material->handle);
            entity.set_transform({.translation = {(index % columns + 0.5f) * spacing - 5, 0,
                                      (index / columns + 0.5f) * spacing - 5},
                .rotation = {0, 20, 0},
                .scale = Comet::Math::Vec3(spacing * 0.65f)});
        }
        auto ground = scene->create_entity("Ground");
        ground.add_component<Comet::MeshRendererComponent>(mesh->handle, material->handle);
        ground.set_transform(
            {.translation = {0, -spacing * 0.325f - 0.1f, 0}, .scale = {12, 0.2f, 12}});
        auto key = scene->create_entity("Directional");
        key.set_transform({.rotation = {-30, -35, 0}});
        auto& directional = key.add_component<Comet::LightComponent>();
        directional.intensity = 4;
        directional.casts_shadow = true;
        auto point = scene->create_entity("Point");
        point.set_transform({.translation = {-3, 2, 0}});
        auto& point_light = point.add_component<Comet::LightComponent>();
        point_light.type = Comet::LightType::Point;
        point_light.color = {1, 0.2f, 0.1f};
        point_light.intensity = 20;
        point_light.range = 8;
        auto spot = scene->create_entity("Spot");
        spot.set_transform({.translation = {3, 4, 3}, .rotation = {-50, 30, 0}});
        auto& spot_light = spot.add_component<Comet::LightComponent>();
        spot_light.type = Comet::LightType::Spot;
        spot_light.color = {0.1f, 0.2f, 1};
        spot_light.intensity = 20;
        spot_light.range = 12;
        engine.set_scene(std::move(scene));
        return Result<void>::success();
    }

    // 只持有测量样本；场景、帧调度和 GPU 查询仍由生产链路拥有。
    class Measurement {
    public:
        Measurement(Comet::Engine& engine, const Options& options)
            : m_engine(engine), m_options(options),
              m_size(engine.get_renderer().get_scene_renderer().get_render_target().get_size()),
              m_generation(engine.get_renderer()
                      .get_render_context()
                      .get_swapchain()
                      .get_active_generation()) {
            m_passes = {"directional shadow", "scene"};
            if(options.bloom)
                m_passes.insert(
                    m_passes.end(), {"bloom extract", "bloom horizontal", "bloom vertical"});
            m_passes.push_back("display");
            for(const auto* name : {"cpu_wall", "cpu_events", "cpu_update", "cpu_prepare",
                    "cpu_render_submit", "cpu_graph", "gpu_graph"})
                m_samples[name].reserve(options.frames);
            for(const auto& pass : m_passes)
                for(const auto* prefix : {"cpu_", "gpu_"})
                    m_samples[prefix + pass].reserve(options.frames);
        }

        Result<void, Comet::Error> sample(Comet::UpdateContext update) {
            auto& renderer = m_engine.get_renderer();
            const auto& snapshot = renderer.get_diagnostics().get_snapshot();
            const auto& frame = m_engine.frame_diagnostics().current();
            if(update.frame_index > 1) {
                // 跳帧会使主循环索引与提交序号分离，不能继续当作同一组完整样本。
                if(!frame || !frame->rendered || frame->frame_index != update.frame_index - 1
                    || !snapshot.cpu || snapshot.cpu->serial != uint64_t(frame->frame_index)
                    || renderer.get_scene_renderer().get_render_target().get_size() != m_size
                    || renderer.get_render_context().get_swapchain().get_active_generation()
                           != m_generation)
                    return Result<void, Comet::Error>::failure(
                        {"Benchmark interrupted by window, frame or presentation changes"});
                if(auto checked = validate_frame(*snapshot.cpu); !checked)
                    return Result<void, Comet::Error>::failure({checked.error()});
                if(in_range(snapshot.cpu->serial)) {
                    m_samples["cpu_wall"].push_back(frame->total_ms);
                    m_samples["cpu_events"].push_back(frame->events_ms);
                    m_samples["cpu_update"].push_back(frame->update_ms);
                    m_samples["cpu_prepare"].push_back(frame->prepare_ms);
                    m_samples["cpu_render_submit"].push_back(frame->render_submit_ms);
                    append_graph("cpu_", *snapshot.cpu);
                }
            }
            if(snapshot.gpu && snapshot.gpu->serial > m_last_gpu) {
                m_last_gpu = snapshot.gpu->serial;
                if(in_range(m_last_gpu))
                    append_graph("gpu_", *snapshot.gpu);
            }
            const auto drain = renderer.get_frame_scheduler().get_frame_slot_count() + 2;
            if(update.frame_index > WARMUP_FRAMES + m_options.frames + drain) {
                m_finished = true;
                m_engine.get_window().request_close();
            }
            return Result<void, Comet::Error>::success();
        }

        Result<void> write_report() {
            if(!m_finished || m_samples.at("cpu_wall").size() != m_options.frames
                || m_samples.at("cpu_graph").size() != m_options.frames)
                return Result<void>::failure("Benchmark ended before collecting all CPU samples");
            for(const auto& [name, values] : m_samples)
                for(const double value : values)
                    if(!std::isfinite(value) || value < 0)
                        return Result<void>::failure("Invalid timing sample: " + name);
            auto& renderer = m_engine.get_renderer();
            auto& device = renderer.get_render_context().get_device();
            const auto properties = device.get_capability().physical_device.getProperties();
            const auto& snapshot = renderer.get_diagnostics().get_snapshot();
            const auto& stats = renderer.get_scene_renderer().get_material_statistics();
            const auto& post_process = renderer.get_scene_renderer().get_post_process_settings();
            const auto gpu_samples = m_samples.at("gpu_graph").size();
            std::string_view gpu_status = "complete";
            if(!snapshot.gpu_supported)
                gpu_status = "unsupported";
            else if(!snapshot.gpu_error.empty())
                gpu_status = "degraded";
            else if(gpu_samples != m_options.frames)
                gpu_status = "partial";
            uint64_t allocated = 0;
            for(const auto& heap : device.query_memory_budget().heaps)
                allocated += heap.allocation_bytes;
            std::ostringstream report;
            report.imbue(std::locale::classic());
            report << "# build=" << COMET_BENCHMARK_BUILD_TYPE << " validation_request=off\n"
                   << "# device=" << properties.deviceName.data()
                   << " driver=" << properties.driverVersion << '\n'
                   << "# objects=" << m_options.objects << " scene_draws=" << stats.draw_calls
                   << " lights=" << stats.light_count << " msaa=4 bloom=" << m_options.bloom
                   << " ibl=off output=sdr\n"
                   << "# exposure=" << post_process.exposure
                   << " bloom_strength=" << post_process.bloom_strength
                   << " bloom_threshold=" << post_process.bloom_threshold << '\n'
                   << "# pipeline_binds=" << stats.pipeline_binds
                   << " material_binds=" << stats.material_binds
                   << " cached_material_versions=" << stats.cached_material_versions << '\n'
                   << "# window=" << m_options.width << 'x' << m_options.height
                   << " framebuffer=" << m_size.x << 'x' << m_size.y
                   << " present_mode=" << vk::to_string(m_generation->get_config().present_mode)
                   << '\n'
                   << "# warmup=" << WARMUP_FRAMES << " requested_samples=" << m_options.frames
                   << " gpu_samples=" << gpu_samples << " gpu_status=" << gpu_status << '\n'
                   << "# gpu_error=" << snapshot.gpu_error << '\n'
                   << "# vma_allocation_bytes=" << allocated << '\n'
                   << "metric,samples,p50_ms,p95_ms\n";
            for(auto& [name, values] : m_samples) {
                if(values.empty())
                    continue;
                std::sort(values.begin(), values.end());
                const auto percentile = [&](double p) {
                    return values[static_cast<size_t>(std::ceil(values.size() * p)) - 1];
                };
                report << name << ',' << values.size() << ',' << percentile(0.5) << ','
                       << percentile(0.95) << '\n';
            }
            const auto written = Comet::write_text_file_atomic(m_options.output, report.str());
            if(written)
                std::cout << report.str();
            return written;
        }

    private:
        bool in_range(uint64_t serial) const {
            return serial > WARMUP_FRAMES && serial <= WARMUP_FRAMES + m_options.frames;
        }
        Result<void> validate_frame(const Comet::RenderDiagnostics::GraphTiming& frame) const {
            const auto& scene = m_engine.get_renderer().get_scene_renderer();
            const auto& stats = scene.get_material_statistics();
            if(frame.truncated || frame.passes.size() != m_passes.size()
                || stats.draw_calls != m_options.objects + 1 || stats.light_count != 3
                || stats.pipeline_binds != 1 || stats.material_binds != 1
                || stats.cached_material_versions != 1
                || scene.get_post_process_settings().uses_bloom() != m_options.bloom)
                return Result<void>::failure(
                    "Benchmark did not execute the expected forward scene");
            for(size_t index = 0; index < m_passes.size(); ++index)
                if(frame.passes[index].name != m_passes[index])
                    return Result<void>::failure("Unexpected benchmark pass order");
            return Result<void>::success();
        }
        void append_graph(
            const std::string& prefix, const Comet::RenderDiagnostics::GraphTiming& frame) {
            m_samples.at(prefix + "graph").push_back(frame.milliseconds);
            for(const auto& pass : frame.passes)
                m_samples.at(prefix + pass.name).push_back(pass.milliseconds);
        }

        Comet::Engine& m_engine;
        const Options& m_options;
        Comet::Math::Vec2u m_size;
        std::shared_ptr<Comet::Swapchain::Generation> m_generation;
        std::vector<std::string> m_passes;
        std::map<std::string, std::vector<double>> m_samples;
        uint64_t m_last_gpu = 0;
        bool m_finished = false;
    };

    Result<void> measure(const Options& options) {
        const auto project = create_temporary_project();
        if(!project)
            return Result<void>::failure(project.error());
        const Comet::ScopeExit cleanup([&] {
            std::error_code error;
            std::filesystem::remove_all(project.value(), error);
        });
        if(auto prepared = prepare_assets(project.value()); !prepared)
            return prepared;
        Comet::Config config;
        config.window.width = static_cast<int>(options.width);
        config.window.height = static_cast<int>(options.height);
        config.window.resizable = false;
        config.window.title = "Comet Forward Benchmark";
        config.vulkan.msaa_samples = Comet::SampleCount::Count4;
        config.vulkan.enable_validation = false;
        config.diagnostics.log.level = "warn";
        config.diagnostics.log.enable_file_logging = false;
        config.diagnostics.enable_render_diagnostics = true;
        Comet::Diagnostics logging(config.diagnostics);
        auto created = Comet::Engine::create(config);
        if(!created)
            return Result<void>::failure(created.error().message);
        auto engine = std::move(created).value();
        Comet::AssetManager assets(Comet::ProjectPaths(project.value()),
            engine->get_asset_registry(), engine->get_render_resources(),
            engine->get_task_scheduler());
        const Comet::ScopeExit shutdown([&] { engine->prepare_shutdown(); });
        if(auto populated = populate_scene(*engine, assets, options); !populated)
            return populated;
        Measurement measurement(*engine, options);
        const auto run = engine->run(
            [&](Comet::Engine::FrameContext& frame) { return measurement.sample(frame.update); });
        if(!run)
            return Result<void>::failure(run.error().message);
        return measurement.write_report();
    }
}

int main(int argc, char** argv) {
    if(argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << USAGE << '\n';
        return 0;
    }
    try {
        const auto options = parse_options(argc, argv);
        if(!options) {
            std::cerr << options.error() << '\n';
            return 2;
        }
        if(const auto result = measure(options.value()); !result) {
            std::cerr << result.error() << '\n';
            return 1;
        }
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
