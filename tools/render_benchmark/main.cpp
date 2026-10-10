#include "asset/asset_manager.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "asset/runtime/render_asset_publisher.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/file_io.h"
#include "common/scope_exit.h"
#include "config/config.h"
#include "core/engine.h"
#include "core/window.h"
#include "diagnostics/diagnostics.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "physics/physics_service.h"
#include "render/frame_scheduler.h"
#include "render/render_context.h"
#include "render/render_diagnostics.h"
#include "render/quality_settings.h"
#include "render/renderer.h"
#include "render/material/material.h"
#include "render/material/material_shader.h"
#include "render/resource/render_resources.h"
#include "render/scene/scene_renderer.h"
#include "render/render_target.h"
#include "scene/scene.h"
#include "scene/systems/physics_system.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <locale>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif

namespace {
    using Comet::Result;
    constexpr unsigned WARMUP_FRAMES = 32;
    constexpr unsigned SETTLE_STEPS = 300;
    constexpr unsigned RESPAWN_STEPS = 24;
    // 默认物理世界的接触缓存需要余量，不能只按刚体容量取上限。
    constexpr unsigned MAX_PHYSICS_OBJECTS = 512;
    constexpr std::string_view USAGE =
        "Usage: render_benchmark OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1) "
        "[MATERIALS [static|moving|light-moving|culling|project-shader|physics-active|physics-sleeping "
        "[MSAA ANISOTROPY RENDER_SCALE]]]";

    enum class Workload {
        Static,
        Moving,
        LightMoving,
        Culling,
        ProjectShader,
        PhysicsActive,
        PhysicsSleeping
    };

    std::string_view workload_name(Workload workload) {
        switch(workload) {
            case Workload::Moving:
                return "moving";
            case Workload::LightMoving:
                return "light-moving";
            case Workload::Culling:
                return "culling";
            case Workload::ProjectShader:
                return "project-shader";
            case Workload::PhysicsActive:
                return "physics-active";
            case Workload::PhysicsSleeping:
                return "physics-sleeping";
            default:
                return "static";
        }
    }

    bool uses_physics(Workload workload) {
        return workload == Workload::PhysicsActive || workload == Workload::PhysicsSleeping;
    }

    struct Options {
        std::filesystem::path output;
        unsigned objects, width, height, frames;
        bool bloom;
        unsigned materials = 1;
        Workload workload = Workload::Static;
        Comet::QualitySettings quality{4, 1, 1};
    };

    Result<Options> parse_options(int argc, char** argv) {
#ifdef __APPLE__
        if(argc >= 3 && std::string_view(argv[argc - 2]) == "-NSAutomaticWindowAnimationsEnabled"
            && std::string_view(argv[argc - 1]) == "NO")
            argc -= 2;
#endif
        if(argc < 7 || (argc > 9 && argc != 12) || std::string_view(argv[1]).empty())
            return Result<Options>::failure(std::string(USAGE));
        const std::array<unsigned, 6> minimum{1, 64, 64, 8, 0, 1};
        const std::array<unsigned, 6> maximum{4096, 4096, 4096, 10000, 1, 256};
        std::array<unsigned, 6> values{0, 0, 0, 0, 0, 1};
        const unsigned numeric_count = argc >= 8 ? 6 : 5;
        for(unsigned index = 0; index < numeric_count; ++index) {
            const std::string_view text(argv[index + 2]);
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), values[index]);
            if(error != std::errc{} || end != text.data() + text.size()
                || values[index] < minimum[index] || values[index] > maximum[index])
                return Result<Options>::failure("Invalid benchmark argument: " + std::string(text));
        }
        Workload workload = Workload::Static;
        if(argc >= 9) {
            const std::string_view name(argv[8]);
            if(name == "culling")
                workload = Workload::Culling;
            else if(name == "moving")
                workload = Workload::Moving;
            else if(name == "light-moving")
                workload = Workload::LightMoving;
            else if(name == "physics-active")
                workload = Workload::PhysicsActive;
            else if(name == "physics-sleeping")
                workload = Workload::PhysicsSleeping;
            else if(name == "project-shader")
                workload = Workload::ProjectShader;
            else if(name != "static")
                return Result<Options>::failure("Invalid benchmark workload: " + std::string(name));
        }
        if(values[5] > values[0])
            return Result<Options>::failure("Material count cannot exceed object count");
        if(uses_physics(workload) && values[0] > MAX_PHYSICS_OBJECTS)
            return Result<Options>::failure(
                "Physics workloads support at most 512 objects plus ground");
        Comet::QualitySettings quality{4, 1, 1};
        if(argc == 12) {
            // 与 JSON 相同的数值范围；采用固定 locale，不受系统小数分隔符影响。
            const auto number = [](const char* text, auto& value) {
                std::istringstream input(text);
                input.imbue(std::locale::classic());
                input >> std::noskipws >> value;
                return input && input.eof();
            };
            if(!number(argv[9], quality.msaa_samples) || !number(argv[10], quality.max_anisotropy)
                || !number(argv[11], quality.render_scale))
                return Result<Options>::failure("Invalid benchmark quality arguments");
            if(auto valid = quality.validate(); !valid)
                return Result<Options>::failure("Invalid benchmark quality: " + valid.error());
        }
        return Result<Options>::success({argv[1], values[0], values[1], values[2], values[3],
            values[4] != 0, values[5], workload, quality});
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

    std::filesystem::path material_path(unsigned index) {
        if(index == 0)
            return "materials/ground.mat";
        return "materials/pbr-" + std::to_string(index) + ".mat";
    }

    Result<void> prepare_assets(const std::filesystem::path& root, const Options& options) {
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
        const auto base = Comet::MaterialSerializer{}.load(root / "assets" / material_path(0));
        if(!base)
            return Result<void>::failure(base.error());
        for(unsigned index = 1; index < options.materials; ++index) {
            auto data = base.value();
            const float t = float(index) / options.materials;
            data.scalar_properties["roughness"] = 0.2f + 0.8f * t;
            data.vector_properties["base_color"] = {
                0.2f + 0.8f * t, 0.9f - 0.5f * t, 0.3f + 0.4f * t, 1};
            const auto path = root / "assets" / material_path(index);
            if(auto saved = Comet::MaterialSerializer{}.save(data, path); !saved)
                return saved;
            if(auto saved = Comet::MetadataSerializer{}.save(
                   {.handle = Comet::AssetHandle(index), .type = Comet::AssetType::Material},
                   Comet::metadata_path(path));
                !saved)
                return saved;
        }
        return Result<void>::success();
    }

    struct InitialPose {
        Comet::Entity entity;
        Comet::TransformComponent transform;
    };

    Result<void> populate_scene(Comet::Engine& engine, Comet::AssetManager& assets,
        const std::filesystem::path& root, const Options& options,
        std::vector<InitialPose>& initial_poses) {
        if(!assets.scan().succeeded())
            return Result<void>::failure("Benchmark asset scan failed");
        const auto* mesh = assets.get_database().find("meshes/cube.gltf");
        if(!mesh)
            return Result<void>::failure("Benchmark assets were not indexed");
        const auto mesh_handle = mesh->handle;
        if(auto imported = assets.import_mesh(mesh_handle); !imported)
            return Result<void>::failure(imported.error().message);
        if(auto loaded = assets.ensure_loaded(mesh_handle, Comet::AssetType::Mesh); !loaded)
            return Result<void>::failure(loaded.error().message);
        std::vector<Comet::AssetHandle> materials;
        Comet::RenderAssetPublisher publisher(
            engine.get_asset_registry(), engine.get_render_resources());
        constexpr Comet::AssetHandle project_program(0xc0be700000000001ULL);
        if(options.workload == Workload::ProjectShader) {
            const auto code = Comet::default_material_shaders().at("pbr");
            auto program = std::make_shared<Comet::ShaderProgramArtifact>();
            program->handle = project_program;
            program->vertex_words = code.vertex;
            program->fragment_words = code.fragment;
            if(!engine.get_asset_registry().register_asset(project_program, std::move(program)))
                return Result<void>::failure("Cannot register benchmark Shader program");
        }
        for(unsigned index = 0; index < options.materials; ++index) {
            const auto* material = assets.get_database().find(material_path(index));
            if(!material)
                return Result<void>::failure("Benchmark material was not indexed");
            materials.push_back(material->handle);
            if(auto loaded = assets.ensure_loaded(material->handle, material->type); !loaded)
                return Result<void>::failure(loaded.error().message);
            if(options.workload == Workload::ProjectShader) {
                auto data =
                    Comet::MaterialSerializer{}.load(root / "assets" / material_path(index));
                if(!data)
                    return Result<void>::failure(data.error());
                data.value().shader_program = project_program;
                std::map<std::string, std::shared_ptr<Comet::Texture>> textures;
                for(const auto& [name, handle] : data.value().texture_properties)
                    textures.emplace(name, publisher.texture(handle));
                auto version = publisher.prepare_material(
                    "benchmark project material", data.value(), textures);
                if(!version)
                    return Result<void>::failure(version.error().message);
                if(!publisher.publish(material->handle, version.value(), true))
                    return Result<void>::failure("Cannot register benchmark project material");
            }
        }
        auto scene = std::make_unique<Comet::Scene>();
        unsigned identity = 0;
        const auto create_entity = [&](const char* name) {
            ++identity;
            Comet::EntityUuid::Bytes bytes{};
            bytes[0] = 0xc0;
            bytes[6] = 0x40;
            bytes[8] = 0x80;
            bytes[14] = static_cast<uint8_t>(identity >> 8);
            bytes[15] = static_cast<uint8_t>(identity);
            return scene->create_entity_with_uuid(Comet::EntityUuid(bytes), name);
        };
        if(!scene->set_post_process({.bloom_enabled = options.bloom}))
            return Result<void>::failure("Invalid benchmark post processing settings");
        auto camera = create_entity("Camera");
        camera.add_component<Comet::CameraComponent>(Comet::CameraComponent{.primary = true});
        camera.set_transform({.translation = {0, 10, 16}, .rotation = {-30, 0, 0}});
        const auto columns = static_cast<unsigned>(std::ceil(std::sqrt(options.objects)));
        const float spacing = 10.0f / columns;
        for(unsigned index = 0; index < options.objects; ++index) {
            auto entity = create_entity("PBR cube");
            entity.add_component<Comet::MeshRendererComponent>(
                mesh_handle, materials[index % materials.size()]);
            entity.set_transform({.translation = {(index % columns + 0.5f) * spacing - 5, 0,
                                      (index / columns + 0.5f) * spacing - 5},
                .rotation = {0, 20, 0},
                .scale = Comet::Math::Vec3(spacing * 0.65f)});
            if(options.workload == Workload::Culling && index >= (options.objects + 3) / 4) {
                auto transform = entity.get_component<Comet::TransformComponent>();
                transform.translation.x += 100;
                entity.set_transform(transform);
            }
            if(uses_physics(options.workload)) {
                entity.add_component<Comet::RigidBodyComponent>();
                entity.add_component<Comet::ColliderComponent>();
                if(options.workload == Workload::PhysicsActive) {
                    auto pose = entity.get_component<Comet::TransformComponent>();
                    pose.translation.y += 1;
                    initial_poses.push_back({entity, pose});
                }
            }
            if(options.workload == Workload::Moving)
                initial_poses.push_back(
                    {entity, entity.get_component<Comet::TransformComponent>()});
        }
        auto ground = create_entity("Ground");
        ground.add_component<Comet::MeshRendererComponent>(mesh_handle, materials.front());
        ground.set_transform(
            {.translation = {0, -spacing * 0.325f - 0.1f, 0}, .scale = {12, 0.2f, 12}});
        if(uses_physics(options.workload)) {
            ground.add_component<Comet::RigidBodyComponent>().motion = Comet::BodyMotion::Static;
            ground.add_component<Comet::ColliderComponent>();
        }
        auto key = create_entity("Directional");
        key.set_transform({.rotation = {-30, -35, 0}});
        key.add_component<Comet::LightComponent>(
            Comet::LightComponent{.intensity = 4, .casts_shadow = true});
        if(options.workload == Workload::LightMoving)
            initial_poses.push_back({key, key.get_component<Comet::TransformComponent>()});
        auto point = create_entity("Point");
        point.set_transform({.translation = {-3, 2, 0}});
        point.add_component<Comet::LightComponent>(
            Comet::LightComponent{.type = Comet::LightType::Point,
                .color = {1, 0.2f, 0.1f},
                .intensity = 20,
                .range = 8});
        auto spot = create_entity("Spot");
        spot.set_transform({.translation = {3, 4, 3}, .rotation = {-50, 30, 0}});
        spot.add_component<Comet::LightComponent>(
            Comet::LightComponent{.type = Comet::LightType::Spot,
                .color = {0.1f, 0.2f, 1},
                .intensity = 20,
                .range = 12});
        engine.set_scene(std::move(scene));
        return Result<void>::success();
    }

    // 只持有测量样本；场景、帧调度和 GPU 查询仍由生产链路拥有。
    class Measurement {
    public:
        Measurement(Comet::Engine& engine, const Options& options)
            : m_engine(engine), m_options(options),
              m_physics(engine.get_scene_runtime().find_system<Comet::PhysicsSystem>()),
              m_warmup(std::max(WARMUP_FRAMES, options.frames / 4)
                       + (options.workload == Workload::PhysicsSleeping ? SETTLE_STEPS : 0)),
              m_size(engine.get_renderer().get_scene_renderer().get_render_target().get_size()),
              m_scene_size(engine.get_renderer().get_scene_size()),
              m_quality(engine.get_renderer().get_quality_settings()),
              m_generation(engine.get_renderer()
                      .get_render_context()
                      .get_swapchain()
                      .get_active_generation()) {
            for(unsigned material = 0; material < options.materials; ++material) {
                const auto instances = options.objects / options.materials
                                       + (material < options.objects % options.materials)
                                       + (material == 0); // 地面使用第一个材质。
                if(instances > 1)
                    m_expected_instance_bytes += instances * sizeof(Comet::Math::Mat4);
            }
            m_passes = {"directional shadow", "scene"};
            if(options.bloom)
                m_passes.insert(
                    m_passes.end(), {"bloom extract", "bloom horizontal", "bloom vertical"});
            m_passes.push_back("display");
            for(const auto* name : {"cpu_wall", "cpu_events", "cpu_update", "cpu_prepare",
                    "cpu_runtime_update", "cpu_render_submit", "cpu_scene_extract",
                    "cpu_asset_resolution", "cpu_material_programs", "cpu_geometry", "cpu_lighting",
                    "cpu_submission_prep", "cpu_queue_submit", "cpu_present", "cpu_render_record",
                    "cpu_graph", "gpu_graph"})
                m_samples[name].reserve(options.frames);
            for(const auto& pass : m_passes)
                for(const auto* prefix : {"cpu_", "gpu_"})
                    m_samples[prefix + pass].reserve(options.frames);
        }

        Result<void, Comet::Error> sample(Comet::UpdateContext update) {
#ifdef __APPLE__
            if(in_range(update.frame_index)) {
                size_t cpu;
                if(const auto error = pthread_cpu_number_np(&cpu); error != 0)
                    return Result<void, Comet::Error>::failure(
                        {"Cannot sample benchmark CPU: " + std::to_string(error)});
                ++m_cpu_samples[cpu];
            }
#endif
            auto& renderer = m_engine.get_renderer();
            const auto& snapshot = renderer.get_diagnostics().get_snapshot();
            const auto& frame = m_engine.frame_diagnostics().current();
            if(update.frame_index > 1) {
                // 跳帧会使主循环索引与提交序号分离，不能继续当作同一组完整样本。
                if(!frame || !frame->rendered || frame->frame_index != update.frame_index - 1
                    || !snapshot.cpu || snapshot.cpu->serial != uint64_t(frame->frame_index)
                    || !snapshot.preparation || snapshot.preparation->serial != snapshot.cpu->serial
                    || !snapshot.submission || snapshot.submission->serial != snapshot.cpu->serial
                    || renderer.get_scene_renderer().get_render_target().get_size() != m_size
                    || renderer.get_scene_size() != m_scene_size
                    || renderer.get_quality_settings() != m_quality || renderer.quality_pending()
                    || renderer.get_render_context().get_swapchain().get_active_generation()
                           != m_generation)
                    return Result<void, Comet::Error>::failure(
                        {"Benchmark interrupted by window, frame or presentation changes"});
                if(auto checked = validate_frame(*snapshot.cpu); !checked)
                    return Result<void, Comet::Error>::failure({checked.error()});
                if(in_range(snapshot.cpu->serial)) {
                    const auto physics = physics_statistics();
                    if(auto checked = validate_workload(physics); !checked)
                        return Result<void, Comet::Error>::failure({checked.error()});
                    m_active_min = std::min(m_active_min, physics.active_bodies);
                    m_active_max = std::max(m_active_max, physics.active_bodies);
                    m_pose_min = std::min(m_pose_min, physics.pose_updates);
                    m_pose_max = std::max(m_pose_max, physics.pose_updates);
                    m_samples["cpu_wall"].push_back(frame->total_ms);
                    m_samples["cpu_events"].push_back(frame->events_ms);
                    m_samples["cpu_update"].push_back(frame->update_ms);
                    m_samples["cpu_prepare"].push_back(frame->prepare_ms);
                    m_samples["cpu_runtime_update"].push_back(frame->runtime_update_ms);
                    m_samples["cpu_render_submit"].push_back(frame->render_submit_ms);
                    m_samples["cpu_scene_extract"].push_back(frame->scene_extract_ms);
                    m_samples["cpu_asset_resolution"].push_back(snapshot.preparation->assets_ms);
                    m_samples["cpu_material_programs"].push_back(
                        snapshot.preparation->material_programs_ms);
                    m_samples["cpu_geometry"].push_back(snapshot.preparation->geometry_ms);
                    m_samples["cpu_lighting"].push_back(snapshot.preparation->lighting_ms);
                    m_samples["cpu_submission_prep"].push_back(snapshot.submission->finalize_ms);
                    m_samples["cpu_queue_submit"].push_back(snapshot.submission->submit_ms);
                    m_samples["cpu_present"].push_back(snapshot.submission->present_ms);
                    m_samples["cpu_render_record"].push_back(
                        frame->render_submit_ms - frame->scene_extract_ms
                        - snapshot.submission->finalize_ms - snapshot.submission->submit_ms
                        - snapshot.submission->present_ms);
                    append_graph("cpu_", *snapshot.cpu);
                }
            }
            if(snapshot.gpu && snapshot.gpu->serial > m_last_gpu) {
                m_last_gpu = snapshot.gpu->serial;
                if(in_range(m_last_gpu))
                    append_graph("gpu_", *snapshot.gpu);
            }
            const auto drain = renderer.get_frame_scheduler().get_frame_slot_count() + 2;
            if(update.frame_index > m_warmup + m_options.frames + drain) {
                m_finished = true;
                m_engine.get_window().request_close();
            }
            return Result<void, Comet::Error>::success();
        }

        Result<void> write_report() {
            if(!m_finished || m_samples.at("cpu_wall").size() != m_options.frames
                || m_samples.at("cpu_graph").size() != m_options.frames)
                return Result<void>::failure("Benchmark ended before collecting all CPU samples");
            for(const auto& [name, values] : m_samples) {
                if(name.starts_with("cpu_") && values.size() != m_options.frames)
                    return Result<void>::failure("Incomplete CPU timing samples: " + name);
                for(const double value : values)
                    if(!std::isfinite(value) || value < 0)
                        return Result<void>::failure("Invalid timing sample: " + name);
            }
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
            report << "# build=" << COMET_BENCHMARK_BUILD_TYPE << " validation_request=off\n";
#ifdef __APPLE__
            report << "# cpu_qos_request=user_interactive\n";
            report << "# cpu_samples=";
            for(const auto& [cpu, samples] : m_cpu_samples)
                report << cpu << ':' << samples << ' ';
            report << '\n';
#endif
            report << "# device=" << properties.deviceName.data()
                   << " driver=" << properties.driverVersion << '\n'
                   << "# objects=" << m_options.objects << " scene_draws=" << stats.draw_calls
                   << " lights=" << stats.light_count << " msaa=" << m_quality.msaa_samples
                   << " bloom=" << m_options.bloom << " ibl=off output=sdr\n"
                   << "# anisotropy=" << m_quality.max_anisotropy
                   << " render_scale=" << m_quality.render_scale << " scene=" << m_scene_size.x
                   << 'x' << m_scene_size.y << '\n'
                   << "# requested_msaa=" << m_options.quality.msaa_samples
                   << " requested_anisotropy=" << m_options.quality.max_anisotropy
                   << " requested_render_scale=" << m_options.quality.render_scale << '\n'
                   << "# render_items=" << stats.render_items
                   << " culled_items=" << stats.culled_items << '\n'
                   << "# workload=" << workload_name(m_options.workload)
                   << " materials=" << m_options.materials << " layout="
                   << (m_options.workload == Workload::Culling ? "culling-v1" : "grid-v1")
                   << " stable_ids=on\n"
                   << "# moving_rotation_degrees_per_frame="
                   << (m_options.workload == Workload::Moving ? 0.5 : 0.0) << '\n'
                   << "# light_rotation_degrees_per_frame="
                   << (m_options.workload == Workload::LightMoving ? 0.5 : 0.0) << '\n'
                   << "# physics_bodies=" << physics_statistics().bodies
                   << " active_bodies_min=" << m_active_min << " active_bodies_max=" << m_active_max
                   << " pose_updates_min=" << m_pose_min << " pose_updates_max=" << m_pose_max
                   << '\n'
                   << "# physics_fixed_delta_s=" << Comet::SceneRuntime::Settings{}.fixed_delta
                   << " physics_steps_per_frame=" << (!uses_physics(m_options.workload) ? 0 : 1)
                   << " settle_steps="
                   << (m_options.workload == Workload::PhysicsSleeping ? SETTLE_STEPS : 0)
                   << " respawn_steps="
                   << (m_options.workload == Workload::PhysicsActive ? RESPAWN_STEPS : 0) << '\n'
                   << "# exposure=" << post_process.exposure
                   << " bloom_strength=" << post_process.bloom_strength
                   << " bloom_threshold=" << post_process.bloom_threshold << '\n'
                   << "# pipeline_binds=" << stats.pipeline_binds
                   << " material_binds=" << stats.material_binds
                   << " cached_material_versions=" << stats.cached_material_versions << '\n'
                   << "# mesh_binds=" << stats.mesh_binds
                   << " material_preparations=" << stats.material_preparations << '\n'
                   << "# drawn_instances=" << stats.drawn_instances
                   << " instanced_draws=" << stats.instanced_draw_calls
                   << " instance_upload_bytes=" << stats.instance_upload_bytes << '\n'
                   << "# shadow_draws="
                   << renderer.get_scene_renderer().get_shadow_statistics().draw_calls
                   << " shadow_instances="
                   << renderer.get_scene_renderer().get_shadow_statistics().drawn_instances
                   << " shadow_upload_bytes="
                   << renderer.get_scene_renderer().get_shadow_statistics().instance_upload_bytes
                   << '\n'
                   << "# window=" << m_options.width << 'x' << m_options.height
                   << " framebuffer=" << m_size.x << 'x' << m_size.y
                   << " present_mode=" << vk::to_string(m_generation->get_config().present_mode)
                   << '\n'
                   << "# warmup=" << m_warmup << " requested_samples=" << m_options.frames
                   << " gpu_samples=" << gpu_samples << " gpu_status=" << gpu_status << '\n'
                   << "# gpu_error=" << snapshot.gpu_error << '\n'
                   << "# vma_allocation_bytes=" << allocated << '\n'
                   << "metric,samples,p50_ms,p95_ms,p99_ms\n";
            for(auto& [name, values] : m_samples) {
                if(values.empty())
                    continue;
                std::sort(values.begin(), values.end());
                const auto percentile = [&](double p) {
                    return values[static_cast<size_t>(std::ceil(values.size() * p)) - 1];
                };
                report << name << ',' << values.size() << ',' << percentile(0.5) << ','
                       << percentile(0.95) << ',' << percentile(0.99) << '\n';
            }
            const auto written = Comet::write_text_file_atomic(m_options.output, report.str());
            if(written)
                std::cout << report.str();
            return written;
        }

    private:
        bool in_range(uint64_t serial) const {
            return serial > m_warmup && serial <= m_warmup + m_options.frames;
        }
        Comet::PhysicsService::Statistics physics_statistics() const {
            return m_physics ? m_physics->get_statistics() : Comet::PhysicsService::Statistics{};
        }
        Result<void> validate_frame(const Comet::RenderDiagnostics::GraphTiming& frame) const {
            const auto& scene = m_engine.get_renderer().get_scene_renderer();
            const auto& stats = scene.get_material_statistics();
            const auto visible_objects = m_options.workload == Workload::Culling
                                             ? (m_options.objects + 3) / 4
                                             : m_options.objects;
            const auto visible_materials = std::min(m_options.materials, visible_objects);
            const auto& shadow = scene.get_shadow_statistics();
            const bool project_shader = m_options.workload == Workload::ProjectShader;
            const auto draws = project_shader ? visible_objects + 1 : visible_materials;
            const auto pipeline_binds =
                project_shader || visible_objects >= 2 * visible_materials - 1 ? 1u : 2u;
            if(frame.truncated || frame.passes.size() != m_passes.size()
                || stats.render_items != m_options.objects + 1
                || stats.culled_items != m_options.objects - visible_objects
                || stats.draw_calls != draws || stats.drawn_instances != visible_objects + 1
                || shadow.draw_calls != 1 || shadow.drawn_instances != m_options.objects + 1
                || stats.light_count != 3 || stats.pipeline_binds != pipeline_binds
                || stats.material_binds != visible_materials
                || stats.material_preparations != visible_materials || stats.mesh_binds != 1
                || stats.cached_material_versions != visible_materials
                || scene.get_post_process_settings().uses_bloom() != m_options.bloom)
                return Result<void>::failure(
                    "Benchmark did not execute the expected forward scene");
            for(size_t index = 0; index < m_passes.size(); ++index)
                if(frame.passes[index].name != m_passes[index])
                    return Result<void>::failure("Unexpected benchmark pass order");
            return Result<void>::success();
        }
        Result<void> validate_workload(const Comet::PhysicsService::Statistics& physics) const {
            if(!uses_physics(m_options.workload)) {
                if(physics.bodies != 0)
                    return Result<void>::failure("Non-physics benchmark unexpectedly ran physics");
                const auto& renderer = m_engine.get_renderer().get_scene_renderer();
                if(m_options.workload == Workload::Moving) {
                    const auto bytes = (m_options.objects + 1) * sizeof(Comet::Math::Mat4);
                    if(renderer.get_material_statistics().instance_upload_bytes
                            != m_expected_instance_bytes
                        || renderer.get_shadow_statistics().instance_upload_bytes != bytes)
                        return Result<void>::failure(
                            "Moving benchmark did not upload changed transforms");
                }
                if(m_options.workload == Workload::LightMoving
                    && renderer.get_material_statistics().instance_upload_bytes != 0)
                    return Result<void>::failure(
                        "Light-moving benchmark unexpectedly uploaded object transforms");
                return Result<void>::success();
            }
            const auto& timing = m_engine.get_scene_runtime().get_timing();
            const auto active =
                m_options.workload == Workload::PhysicsActive ? m_options.objects : 0;
            if(physics.bodies != m_options.objects + 1 || physics.active_bodies != active
                || timing.fixed_steps != 1 || timing.dropped_time != 0
                || (m_options.workload == Workload::PhysicsSleeping && physics.pose_updates != 0))
                return Result<void>::failure(
                    "Benchmark physics workload did not maintain its expected state");
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
        const Comet::PhysicsSystem* m_physics;
        unsigned m_warmup;
        Comet::Math::Vec2u m_size;
        Comet::Math::Vec2u m_scene_size;
        Comet::QualitySettings m_quality;
        std::shared_ptr<Comet::Swapchain::Generation> m_generation;
        std::vector<std::string> m_passes;
        std::map<std::string, std::vector<double>> m_samples;
#ifdef __APPLE__
        std::map<size_t, unsigned> m_cpu_samples;
#endif
        std::size_t m_expected_instance_bytes = 0;
        uint64_t m_last_gpu = 0;
        std::size_t m_active_min = std::numeric_limits<std::size_t>::max();
        std::size_t m_active_max = 0;
        std::size_t m_pose_min = std::numeric_limits<std::size_t>::max();
        std::size_t m_pose_max = 0;
        bool m_finished = false;
    };

    Result<void> measure(const Options& options) {
#ifdef __APPLE__
        if(const auto error = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
            error != 0)
            return Result<void>::failure(
                "Cannot set benchmark thread QoS: " + std::to_string(error));
#endif
        const auto project = create_temporary_project();
        if(!project)
            return Result<void>::failure(project.error());
        const Comet::ScopeExit cleanup([&] {
            std::error_code error;
            std::filesystem::remove_all(project.value(), error);
        });
        if(auto prepared = prepare_assets(project.value(), options); !prepared)
            return prepared;
        Comet::Config config;
        config.window.width = static_cast<int>(options.width);
        config.window.height = static_cast<int>(options.height);
        config.window.resizable = false;
        config.window.title = "Comet Forward Benchmark";
        config.vulkan.msaa_samples = static_cast<Comet::SampleCount>(options.quality.msaa_samples);
        config.render.max_anisotropy = options.quality.max_anisotropy;
        config.render.render_scale = options.quality.render_scale;
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
        std::vector<InitialPose> initial_poses;
        if(auto populated =
                populate_scene(*engine, assets, project.value(), options, initial_poses);
            !populated)
            return populated;
        auto& runtime = engine->get_scene_runtime();
        if(uses_physics(options.workload)) {
            if(auto added = engine->add_default_scene_systems(); !added)
                return Result<void>::failure(added.error().message);
            if(auto started = engine->start_scene_runtime(Comet::SceneRuntime::State::Paused);
                !started)
                return Result<void>::failure(started.error().message);
        }
        Measurement measurement(*engine, options);
        const auto run = engine->run({
            .update =
                [&](const Comet::Engine::FrameContext& frame) {
                    if(auto sampled = measurement.sample(frame.update); !sampled)
                        return sampled;
                    if(options.workload == Workload::Moving
                        || options.workload == Workload::LightMoving) {
                        const auto angle = std::fmod(frame.update.frame_index * 0.5f, 360.0f);
                        for(const auto& pose : initial_poses) {
                            auto transform = pose.transform;
                            transform.rotation.y += angle;
                            pose.entity.set_transform(transform);
                        }
                    }
                    if(!uses_physics(options.workload))
                        return Result<void, Comet::Error>::success();
                    if(options.workload == Workload::PhysicsActive
                        && runtime.get_timing().fixed_index % RESPAWN_STEPS == 0)
                        for(const auto& body : initial_poses)
                            body.entity.set_transform(body.transform);
                    return engine->request_runtime_step();
                },
        });
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
