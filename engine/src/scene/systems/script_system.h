#pragma once

#include "scene/systems/system.h"
#include "scene/entity.h"
#include "scripting/script.h"
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace Comet {
    class AssetRegistry;

    class COMET_API ScriptSystem final: public System {
    public:
        explicit ScriptSystem(
            const AssetRegistry& assets, const MaterialParameterValidator* materials = nullptr)
            : m_assets(assets), m_materials(materials) {}
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
            std::weak_ptr<const Script> failed_reload;
        };
        bool is_live(const Key& key, const Entry& entry) const;
        Result<Entry, Error> prepare_entry(
            Entity entity, std::shared_ptr<const Script> script, ParameterMap overrides) const;
        Result<void, Error> reload_changed_scripts();
        Result<void, Error> synchronize(Scene& scene);
        Result<void, Error> dispatch(Scene& scene, const Context& context, Script::Phase phase);
        Result<void, Error> invoke(const Key& key, Entry& entry, Script::Phase phase,
            const Context* context = nullptr, Entity contact_other = {},
            std::string_view event_handler = {}, const ParameterValue* event_value = nullptr);
        Result<void, Error> dispatch_contacts(Scene& scene, const Context& context);
        Result<void, Error> dispatch_events(Scene& scene, const Context& context);
        void stop_entry(const Key& key, Entry& entry) noexcept;
        void stop_all() noexcept;

        const AssetRegistry& m_assets;
        const MaterialParameterValidator* m_materials;
        std::map<Key, Entry> m_entries;
        std::vector<Key> m_start_order;
        Scene* m_scene = nullptr;
    };
}
