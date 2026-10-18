#include "render/scene/render_frontend.h"
#include "diagnostics/logger.h"

namespace Comet {
    void RenderFrontend::add_view(uint32_t id, bool visible) {
        m_views.emplace(id, View{.parameters = {.visible = visible}});
    }
    void RenderFrontend::remove_view(uint32_t id) {
        m_views.erase(id);
    }
    RenderFrontend::View& RenderFrontend::view(uint32_t id) {
        const auto found = m_views.find(id);
        if(found == m_views.end())
            LOG_FATAL("Unknown render frontend view {}", id);
        return found->second;
    }
    const RenderFrontend::View& RenderFrontend::view(uint32_t id) const {
        const auto found = m_views.find(id);
        if(found == m_views.end())
            LOG_FATAL("Unknown render frontend view {}", id);
        return found->second;
    }
    void RenderFrontend::discard_requests() {
        for(auto& [id, view] : m_views) {
            view.pick_request.reset();
            view.lines.clear();
        }
    }
    void RenderFrontend::set_irradiance_support_error(std::string error) {
        m_irradiance_support_error = std::move(error);
    }
    void RenderFrontend::set_sources(RaySceneBuilder::MeshReader read,
        RaySceneBuilder::MeshRevision revision, RaySceneBuilder::Images images,
        std::string support_error) {
        m_ray_read = std::move(read);
        m_ray_revision = std::move(revision);
        m_ray_images = std::move(images);
        m_irradiance_builder.reset();
        m_irradiance_input.reset();
        m_irradiance_error.clear();
        m_path_trace_support_error = std::move(support_error);
        for(auto& [id, view] : m_views) {
            view.path_trace_builder.reset();
            view.path_trace_error.clear();
        }
    }
    void RenderFrontend::prepare_irradiance(RenderFrame& frame) {
        frame.irradiance.reset();
        const auto report = [&](std::string error) {
            if(error != m_irradiance_error && !error.empty())
                LOG_ERROR("实时探针 GI 准备失败：{}", error);
            m_irradiance_error = std::move(error);
        };
        std::vector<RenderIrradianceVolume> selected;
        if(frame.world && frame.has_raster_view())
            for(const auto& volume : frame.world->irradiance_volumes)
                if(volume.volume.realtime) {
                    if(auto valid = volume.volume.realtime_dimensions(); !valid) {
                        report(valid.error());
                        return;
                    }
                    if(selected.size() == LightingData::MAX_IRRADIANCE_VOLUMES) {
                        report("实时 GI 最多支持 4 个区域，请禁用多余区域");
                        return;
                    }
                    selected.push_back(volume.volume);
                }
        if(selected.empty()) {
            m_irradiance_builder.reset();
            m_irradiance_input.reset();
            report({});
            return;
        }
        if(!frame.world->resolution.pending_asset_slots.empty())
            return;
        if(!m_irradiance_support_error.empty()) {
            report(m_irradiance_support_error);
            return;
        }
        if(!m_irradiance_builder) {
            m_irradiance_builder = std::make_unique<RaySceneBuilder>(512);
            m_irradiance_input = std::make_unique<RenderWorld>();
        }
        // 从同一份已解析世界筛选追踪几何，不再次提取 Scene，也不按视图重复构建。
        auto& input = *m_irradiance_input;
        input.scene_lifetime = frame.world->scene_lifetime;
        input.environment = frame.world->environment;
        input.environment.fog_enabled = false;
        input.environment.background = false;
        input.environment.background_color = {};
        input.environment.intensity = 1;
        input.environment_revision = frame.world->environment_revision;
        if(!input.environment.lighting)
            input.environment.asset = {};
        input.lights = frame.world->lights;
        input.render_items.clear();
        for(const auto& item : frame.world->render_items)
            if(item.contributes_to_gi && item.mesh)
                input.render_items.push_back(item);
        auto scene = m_irradiance_builder->update(input, m_ray_read, m_ray_revision, m_ray_images);
        report(scene ? std::string{} : scene.error());
        if(scene)
            frame.irradiance =
                RenderFrame::IrradianceInput{std::move(selected), std::move(scene).value()};
    }

    void RenderFrontend::prepare(RenderFrame& frame) {
        std::shared_ptr<const RayScene> particle_collisions;
        if(frame.world && !frame.world->particle_meshes.empty()) {
            auto built = m_particle_collision_builder.update(frame.world->scene_lifetime,
                frame.world->particle_meshes, m_ray_read, m_ray_revision);
            const auto error = built ? std::string{} : built.error().message;
            if(error != m_particle_collision_error && !error.empty())
                LOG_ERROR("GPU particle collision: {}", error);
            m_particle_collision_error = error;
            if(built)
                particle_collisions = std::move(built).value();
        } else {
            m_particle_collision_builder.reset();
            m_particle_collision_error.clear();
        }
        for(auto& input : frame.views) {
            auto& view = this->view(input.id);
            input.parameters.particle_collisions = particle_collisions;
            view.path_trace_error.clear();
            if(input.draw && frame.world && !input.parameters.path_trace_override
                && !input.parameters.path_trace_enabled
                && frame.world->post_process.path_trace.enabled) {
                input.parameters.path_trace_enabled = true;
                input.parameters.path_trace_settings = frame.world->post_process.path_trace;
            }
            if(!input.draw || !input.parameters.path_trace_enabled || !frame.world) {
                view.path_trace_builder.reset();
                continue;
            }
            input.parameters.path_trace.reset();
            if(frame.world->environment.fog_enabled
                || (frame.world->particles
                    && (!frame.world->particles->instances.empty()
                        || !frame.world->particles->gpu_emitters.empty()))) {
                view.path_trace_error = "路径追踪暂不支持距离雾／粒子；介质请在路径追踪设置中配置";
                continue;
            }
            if(!m_path_trace_support_error.empty()) {
                view.path_trace_error = m_path_trace_support_error;
                continue;
            }
            if(!frame.world->resolution.pending_asset_slots.empty()) {
                view.path_trace_error = "场景资源尚未加载，路径追踪等待导入完成";
                continue;
            }
            if(!view.path_trace_builder)
                view.path_trace_builder = std::make_unique<RaySceneBuilder>();
            auto scene = view.path_trace_builder->update(
                *frame.world, m_ray_read, m_ray_revision, m_ray_images);
            if(scene)
                input.parameters.path_trace = std::move(scene).value();
            else
                view.path_trace_error = scene.error();
        }
        // 路径追踪准备失败时仍绘制光栅；据最终视图路径决定是否需要 GI 输入。
        prepare_irradiance(frame);
    }

}
