#include "runtime/runtime.h"
#include "scene/scene.h"

#include <chrono>
#include <iostream>

namespace {
    class CacheProbe final: public Comet::Application {
    public:
        Comet::PipelineCache::LoadStatus status{};
        std::filesystem::path path;

        void on_init() override {
            const auto& cache = get_engine()
                                    .get_renderer()
                                    .get_render_context()
                                    .get_device()
                                    .get_pipeline_cache();
            status = cache.get_load_status();
            path = cache.get_path();
            auto scene = std::make_unique<Comet::Scene>();
            scene->create_entity("Camera")
                .add_component<Comet::CameraComponent>()
                .primary = true;
            get_engine().set_scene(std::move(scene));
        }
        void on_update(Comet::UpdateContext) override {
            Comet::LineDrawList lines;
            static_cast<void>(
                lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {0, 1, 0, 1}));
            get_engine().get_renderer().submit_lines(lines);
            glfwSetWindowShouldClose(get_engine().get_window().get(), GLFW_TRUE);
        }
        void on_shutdown() override {}
    };
}

int main(int argc, char** argv) {
    if(argc != 4)
        return 2;
    try {
        CacheProbe app;
        const auto began = std::chrono::steady_clock::now();
        const auto result = Comet::run(&app, {.config_directory = argv[1],
                                                 .config_profile = "probe",
                                                 .cache_directory = argv[2]});
        auto expected = Comet::PipelineCache::LoadStatus::Missing;
        if(std::string_view(argv[3]) == "restored")
            expected = Comet::PipelineCache::LoadStatus::Restored;
        else if(std::string_view(argv[3]) != "missing")
            return 2;
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - began)
                                 .count();
        std::cout << "cache status=" << static_cast<int>(app.status)
                  << " process_ms=" << elapsed << '\n';
        return result != 0 || app.status != expected
               || app.path.parent_path() != std::filesystem::path(argv[2]) / "vulkan"
               || !std::filesystem::is_regular_file(app.path);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
