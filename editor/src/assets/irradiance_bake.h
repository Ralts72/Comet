#pragma once

#include "assets/lighting_bake_input.h"
#include "asset/irradiance_bake.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class TaskScheduler;
}

namespace CometEditor {
    class CommandHistory;
    class EditorAssets;

    // Main 拥有编辑与发布；后台任务只持有不可变 CPU 输入和取消标记。
    class IrradianceBake {
    public:
        struct Input {
            Comet::RenderScene scene;
            Comet::IrradianceBake::Settings settings;
            std::string fingerprint;
            std::vector<Comet::AssetHandle> dependencies;
        };
        struct Report {
            Comet::IrradianceBake::Statistics statistics;
            std::size_t input_bytes = 0;
            std::size_t product_bytes = 0;
            double seconds = 0;
        };
        struct Progress {
            uint32_t completed = 0;
            uint32_t total = 0;
        };

        IrradianceBake(EditorAssets& assets, const Comet::AssetRegistry& registry,
            Comet::TaskScheduler& scheduler);
        ~IrradianceBake();
        [[nodiscard]] static Comet::Result<Input> describe_input(
            Comet::Scene& scene, Comet::EntityUuid entity, const Comet::AssetDatabase& assets);
        [[nodiscard]] Comet::Result<void> start(
            Comet::Scene& scene, Comet::EntityUuid entity, const CommandHistory& history);
        [[nodiscard]] Comet::Result<bool> update(Comet::Scene& scene, CommandHistory& history);
        [[nodiscard]] std::string status(Comet::Scene& scene, Comet::EntityUuid entity);
        [[nodiscard]] Progress progress() const;
        [[nodiscard]] bool busy() const;
        [[nodiscard]] const std::optional<Report>& last_report() const { return m_report; }
        void cancel();

    private:
        struct Job;
        EditorAssets& m_assets;
        const Comet::AssetRegistry& m_registry;
        Comet::TaskScheduler& m_scheduler;
        std::unique_ptr<Job> m_job;
        LightingBakeInput::StatusCache m_status;
        std::optional<Report> m_report;
    };
}
