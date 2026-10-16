#include "assets/navigation_bake.h"
#include "assets/editor_assets.h"
#include "core/task_scheduler.h"
#include "asset/navigation_bake.h"
#include "scene/command_history.h"
#include "scene/scene.h"

#include <chrono>
#include <future>

namespace CometEditor {
    using namespace Comet;
    namespace {
        struct Output {
            NavigationData data;
            NavigationBake::Report report;
        };
        Result<Comet::NavigationBake::Input> capture(EditorAssets& assets, Scene& scene,
            const ComponentRegistry& components, EntityUuid entity) {
            auto input = Comet::NavigationBake::capture(scene, components, entity,
                {[&](AssetHandle handle) { return assets.read_mesh_data(handle); },
                    [&](AssetHandle handle) { return assets.mesh_data_revision(handle); }});
            if(!input)
                return Result<Comet::NavigationBake::Input>::failure(input.error().message);
            return Result<Comet::NavigationBake::Input>::success(std::move(input).value());
        }
    }
    struct NavigationBake::Job {
        std::future<Result<Output>> completion;
        EntityUuid entity;
        uint64_t lifetime;
        uint64_t generation;
        uint64_t fingerprint;
        AssetHandle previous;
    };

    NavigationBake::NavigationBake(
        EditorAssets& assets, TaskScheduler& scheduler, const ComponentRegistry& components)
        : m_assets(assets), m_scheduler(scheduler), m_components(components) {}
    NavigationBake::~NavigationBake() = default;

    Result<void> NavigationBake::start(
        Scene& scene, const EntityUuid entity, const CommandHistory& history) {
        if(busy() || history.get_scene() != &scene)
            return Result<void>::failure("导航正在烘焙，或场景已切换");
        auto input = capture(m_assets, scene, m_components, entity);
        if(!input)
            return Result<void>::failure(input.error());
        const auto fingerprint = input.value().fingerprint;
        const auto previous = input.value().settings.data;
        auto task =
            m_scheduler.try_submit_result([input = std::move(input).value()]() -> Result<Output> {
                try {
                    const auto start = std::chrono::steady_clock::now();
                    auto data = Comet::NavigationBake::bake(input);
                    if(!data)
                        return Result<Output>::failure(data.error());
                    Report report{input.triangles.size() / 3, data.value().tile.size(),
                        std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count()};
                    return Result<Output>::success({std::move(data).value(), report});
                } catch(const std::exception& error) {
                    return Result<Output>::failure(error.what());
                }
            });
        if(!task)
            return Result<void>::failure("后台任务队列已满，请稍后重试导航烘焙");
        m_job = std::make_unique<Job>(Job{std::move(*task), entity, scene.get_lifetime(),
            history.generation(), fingerprint, previous});
        return Result<void>::success();
    }

    Result<bool> NavigationBake::update(
        Scene& scene, CommandHistory& history, PropertyEditTransaction& edit) {
        if(!m_job)
            return Result<bool>::success(false);
        if(scene.get_lifetime() != m_job->lifetime || history.get_scene() != &scene
            || history.generation() != m_job->generation) {
            cancel();
            return Result<bool>::success(false);
        }
        if(m_job->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return Result<bool>::success(false);
        auto job = std::move(m_job);
        auto result = job->completion.get();
        if(!result)
            return Result<bool>::failure(result.error());
        if(!edit.commit())
            return Result<bool>::failure("无法结束当前属性编辑");
        const auto input = capture(m_assets, scene, m_components, job->entity);
        if(!input || input.value().fingerprint != job->fingerprint
            || input.value().settings.data != job->previous)
            return Result<bool>::failure("导航烘焙期间几何、区域或产品选择已变化，请重新烘焙");
        const auto path =
            std::filesystem::path("baked")
            / ("nav_" + job->entity.to_string() + "_" + std::to_string(job->fingerprint) + ".nav");
        auto* record = m_assets.database().find(path);
        if(record) {
            const auto existing = NavigationData::load(m_assets.database().paths().assets() / path);
            if(record->type != AssetType::Navigation || !existing
                || existing.value().input_fingerprint != job->fingerprint)
                return Result<bool>::failure("目标导航产品已损坏，请删除该资产后重新烘焙");
        } else {
            const auto report = m_assets.create_navigation(path, result.value().data);
            if(!report.succeeded())
                return Result<bool>::failure(report.issues.front().message);
            record = m_assets.database().find(path);
        }
        if(!record || !edit.apply({job->entity, "navigation_region", "data"}, record->handle))
            return Result<bool>::failure("无法记录导航产品引用；生成的资产仍保留在项目中");
        m_report = result.value().report;
        return Result<bool>::success(true);
    }

    std::string NavigationBake::status(Scene& scene, const EntityUuid uuid) const {
        if(m_job && m_job->entity == uuid)
            return "正在后台构建导航产品…";
        const auto entity = scene.find_entity(uuid);
        if(!entity || !entity.has_component<NavigationRegionComponent>())
            return {};
        const auto asset = entity.get_component<NavigationRegionComponent>().data;
        if(!asset)
            return "未指定离线产品；运行时会在启动准备阶段构建导航。";
        const auto* record = m_assets.database().find(asset);
        if(!record || record->type != AssetType::Navigation)
            return "导航产品缺失，请重新烘焙。";
        return "已指定离线产品；启动时校验静态碰撞几何与区域设置，变化后需重新烘焙。";
    }
    bool NavigationBake::busy() const {
        return bool(m_job);
    }
    void NavigationBake::cancel() {
        m_job.reset();
    }
}
