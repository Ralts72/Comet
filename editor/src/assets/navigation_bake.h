#pragma once

#include "common/result.h"
#include "scene/entity_uuid.h"

#include <memory>
#include <optional>
#include <string>

namespace Comet {
    class Scene;
    class ComponentRegistry;
    class TaskScheduler;
}
namespace CometEditor {
    class EditorAssets;
    class CommandHistory;
    class PropertyEditTransaction;
    class NavigationBake {
    public:
        struct Report {
            size_t triangles = 0;
            size_t product_bytes = 0;
            double milliseconds = 0;
        };
        NavigationBake(EditorAssets& assets, Comet::TaskScheduler& scheduler,
            const Comet::ComponentRegistry& components);
        ~NavigationBake();
        [[nodiscard]] Comet::Result<void> start(
            Comet::Scene& scene, Comet::EntityUuid region, const CommandHistory& history);
        [[nodiscard]] Comet::Result<bool> update(
            Comet::Scene& scene, CommandHistory& history, PropertyEditTransaction& edit);
        [[nodiscard]] std::string status(Comet::Scene& scene, Comet::EntityUuid region) const;
        [[nodiscard]] bool busy() const;
        [[nodiscard]] const std::optional<Report>& last_report() const { return m_report; }
        void cancel();

    private:
        struct Job;
        EditorAssets& m_assets;
        Comet::TaskScheduler& m_scheduler;
        const Comet::ComponentRegistry& m_components;
        std::unique_ptr<Job> m_job;
        std::optional<Report> m_report;
    };
}
