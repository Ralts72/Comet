#pragma once

#include "scene/systems/system.h"
#include "scene/entity.h"
#include "scripting/script.h"
#include <map>
#include <optional>
#include <vector>

namespace Comet {
    class AssetRegistry;

    class COMET_API ScriptSystem final: public System {
    public:
        explicit ScriptSystem(const AssetRegistry& assets) : m_assets(assets) {}
        ~ScriptSystem() override;
        Result<void, Error> on_start(Scene& scene) override;
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override;
        Result<void, Error> update(Scene& scene, const Context& context) override;
        void on_stop(Scene&) noexcept override;

    private:
        struct Key {
            EntityUuid entity;
            uint64_t lifetime;
            AssetHandle asset;
            auto operator<=>(const Key&) const = default;
        };
        struct Entry {
            Entity entity;
            std::shared_ptr<const Script> script;
            std::unique_ptr<Script::Instance> instance;
            ParameterMap parameters;
            std::optional<ParameterMap> overrides;
        };
        bool is_live(const Key& key, const Entry& entry) const;
        Result<void, Error> synchronize(Scene& scene);
        Result<void, Error> dispatch(Scene& scene, const Context& context, Script::Phase phase);
        Result<void, Error> invoke(
            const Key& key, Entry& entry, Script::Phase phase, const Context* context = nullptr,
            Entity contact_other = {});
        Result<void, Error> dispatch_contacts(Scene& scene, const Context& context);
        void stop_entry(const Key& key, Entry& entry) noexcept;
        void stop_all() noexcept;

        const AssetRegistry& m_assets;
        std::map<Key, Entry> m_entries;
        std::vector<Key> m_start_order;
        Scene* m_scene = nullptr;
    };
}
