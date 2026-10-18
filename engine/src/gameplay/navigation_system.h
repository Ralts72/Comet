#pragma once

#include "gameplay/navigation_service.h"
#include "scene/systems/system.h"

namespace Comet::Gameplay {
    class COMET_API NavigationSystem final: public System {
    public:
        explicit NavigationSystem(NavigationService::GeometrySource geometry,
            NavigationService::ProductSource products = {})
            : m_service(std::move(geometry), std::move(products)) {}
        [[nodiscard]] NavigationService& service() { return m_service; }
        Result<void, Error> on_start(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override {
            return m_service.begin(scene);
        }
        Result<void, Error> on_runtime_ready(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override {
            return m_service.prepare_startup(scene);
        }
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override {
            return m_service.advance(scene, context.total_time, context.delta_time);
        }
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override {
            m_service.end();
        }

    private:
        NavigationService m_service;
    };
}
