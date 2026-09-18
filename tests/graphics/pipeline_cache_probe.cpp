#include "runtime/runtime.h"
#include "core/window.h"
#include "graphics/device.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "scene/scene.h"

#include <iostream>

namespace {
    class CacheProbe final: public Comet::Application {
    public:
        explicit CacheProbe(const std::filesystem::path& directory) : Application(directory) {}
        Comet::PipelineCache::LoadStatus status{};
        std::filesystem::path path;
        bool rendered = false;

        Comet::Result<void, Comet::Error> on_init() override {
            const auto& cache = get_engine().get_renderer().get_render_context()
                                    .get_device().get_pipeline_cache();
            status = cache.get_load_status();
            path = cache.get_path();
            auto scene = std::make_unique<Comet::Scene>();
            scene->create_entity("Camera").add_component<Comet::CameraComponent>().primary = true;
            get_engine().set_scene(std::move(scene));
            return Comet::Result<void, Comet::Error>::success();
        }
        Comet::Result<void, Comet::Error> on_frame_ready() override {
            Comet::LineDrawList lines;
            if(!lines.add_line({-0.5f, 0, -2}, {0.5f, 0, -2}, {0, 1, 0, 1}))
                return Comet::Result<void, Comet::Error>::failure({"Cannot submit probe geometry"});
            get_engine().get_renderer().submit_lines(lines);
            rendered = true;
            get_engine().get_window().request_close();
            return Comet::Result<void, Comet::Error>::success();
        }
        Comet::Result<void, Comet::Error> on_shutdown() override {
            return Comet::Result<void, Comet::Error>::success();
        }
    };
}

int main(int argc, char** argv) {
    if(argc != 4)
        return 2;
    CacheProbe app(argv[2]);
    auto expected = Comet::PipelineCache::LoadStatus::Missing;
    if(std::string_view(argv[3]) == "restored")
        expected = Comet::PipelineCache::LoadStatus::Restored;
    else if(std::string_view(argv[3]) != "missing")
        return 2;
    const auto result = Comet::run(&app,
        {.config_directory = argv[1], .config_profile = "probe"});
    std::cout << "cache status=" << static_cast<int>(app.status) << '\n';
    return result != 0 || !app.rendered || app.status != expected
        || app.path.parent_path() != std::filesystem::path(argv[2]) / "vulkan"
        || !std::filesystem::is_regular_file(app.path);
}
