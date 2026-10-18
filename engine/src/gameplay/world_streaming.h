#pragma once
#include "scene/systems/system.h"
#include "scene/entity_uuid.h"
#include "asset/reference.h"
#include <functional>
#include <span>
#include <vector>
#include <memory>
#include <map>

namespace Comet {
    class Prefab;
    class ComponentRegistry;
}
namespace Comet::Gameplay {
    // 玩法决定空间需求；项目宿主提供已有资产准备／就绪能力。
    class COMET_API WorldStreamingSystem final: public System {
    public:
        struct Sources {
            std::function<Result<std::shared_ptr<const Prefab>>(AssetHandle)> load;
            std::function<Result<std::vector<AssetReference>, Error>(Scene&)> prepare;
            std::function<Result<bool, Error>(std::span<const AssetReference>)> ready;
            std::function<uint64_t()> resident_bytes;
        };
        explicit WorldStreamingSystem(
            Sources sources, uint64_t budget_bytes = 512ull * 1024 * 1024);
        ~WorldStreamingSystem();
        Result<void, Error> post_update(Scene&, const Context&) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;
        [[nodiscard]] uint64_t revision() const { return m_revision; }
        [[nodiscard]] std::vector<AssetReference> references() const;
        struct Statistics {
            uint32_t active = 0, pending = 0, failed = 0;
            uint64_t gpu_bytes = 0, budget_bytes = 0;
        };
        [[nodiscard]] Statistics statistics() const;

    private:
        struct Cell;
        bool unload(Scene&, Cell&);
        Sources m_sources;
        uint64_t m_budget_bytes, m_revision = 0;
        std::shared_ptr<const ComponentRegistry> m_components;
        std::map<EntityUuid, std::unique_ptr<Cell>> m_cells;
    };
}
