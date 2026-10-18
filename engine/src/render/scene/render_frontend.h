#pragma once

#include "render/render_frame.h"
#include "render/scene/render_submission.h"
#include "render/particle_collision_scene.h"
#include "render/ray_scene.h"
#include <map>

namespace Comet {
    // Main 的视图请求与 CPU 场景准备；GPU 录制仅接收其产生的 RenderFrame。
    class RenderFrontend final {
    public:
        struct View {
            RenderView parameters;
            std::optional<RenderFrame::Pick> pick_request;
            LineDrawList lines;
            std::unique_ptr<RaySceneBuilder> path_trace_builder;
            std::string path_trace_error;
        };
        void add_view(uint32_t id, bool visible = true);
        void remove_view(uint32_t id);
        [[nodiscard]] View& view(uint32_t id);
        [[nodiscard]] const View& view(uint32_t id) const;
        void set_sources(RaySceneBuilder::MeshReader read, RaySceneBuilder::MeshRevision revision,
            RaySceneBuilder::Images images, std::string support_error);
        void set_irradiance_support_error(std::string error);
        void prepare(RenderFrame& frame);
        void discard_requests();

    private:
        void prepare_irradiance(RenderFrame& frame);
        std::map<uint32_t, View> m_views;
        ParticleCollisionBuilder m_particle_collision_builder;
        std::string m_particle_collision_error;
        RaySceneBuilder::MeshReader m_ray_read;
        RaySceneBuilder::MeshRevision m_ray_revision;
        RaySceneBuilder::Images m_ray_images;
        std::string m_path_trace_support_error;
        std::unique_ptr<RaySceneBuilder> m_irradiance_builder;
        std::unique_ptr<RenderWorld> m_irradiance_input;
        std::string m_irradiance_error;
        std::string m_irradiance_support_error;
    };
}
