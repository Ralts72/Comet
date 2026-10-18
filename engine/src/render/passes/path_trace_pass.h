#pragma once

#include "render/ray_scene.h"
#include "render/render_graph.h"
#include "render/scene/render_types.h"
#include "render/scene/render_scene.h"

namespace Comet {
    class Device;
    class ImageView;
    class RenderDiagnostics;
    class RadianceFilter;
    struct RaySceneBuffers;

    class COMET_API PathTracePass {
    public:
        struct Statistics {
            uint32_t samples = 0;
            uint32_t triangles = 0;
            uint32_t nodes = 0;
            Math::Vec2u size{};
            uint64_t upload_bytes = 0;
            uint64_t geometry_upload_bytes = 0;
            uint64_t dispatches = 0;
        };
        explicit PathTracePass(Device& device);
        ~PathTracePass();
        [[nodiscard]] static Result<void, GraphicsError> check_support(const Device& device);
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            const std::shared_ptr<const RayScene>& scene, const ViewProjectMatrix& camera,
            const PathTraceSettings& settings, Math::Vec2u output_size,
            const RenderGraph::RecordPass& display, RenderDiagnostics* diagnostics = nullptr);
        [[nodiscard]] std::shared_ptr<ImageView> output() const;
        [[nodiscard]] Statistics statistics() const;
        void reset();

    private:
        struct Program;
        struct Resources;
        Result<void, GraphicsError> initialize();
        Result<void, GraphicsError> prepare(const std::shared_ptr<const RayScene>& scene,
            Math::Vec2u size, uint32_t frame_slots, bool guides);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::shared_ptr<Resources> m_resources;
        Math::Mat4 m_view_projection{0};
        PathTraceSettings m_settings;
        ViewProjectMatrix m_previous_camera{Math::Mat4(1), Math::Mat4(1)};
        std::shared_ptr<RaySceneBuffers> m_previous_buffers;
        std::unique_ptr<RadianceFilter> m_filter;
        uint32_t m_samples = 0;
        uint64_t m_recorded_serial = 0;
        uint64_t m_upload_bytes = 0;
        uint64_t m_geometry_upload_bytes = 0;
        uint64_t m_dispatches = 0;
    };
}
