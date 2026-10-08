#pragma once

#include "scene/systems/system.h"
#include "scene/entity.h"
#include "scripting/script.h"
#include "scripting/script_assets.h"
#include "scripting/script_runtime_view.h"
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace Comet {
    class COMET_API ScriptSystem final: public System, public ScriptRuntimeView {
    public:
        explicit ScriptSystem(
            ScriptAssets assets, const MaterialParameterValidator* materials = nullptr)
            : m_assets(assets), m_materials(materials) {}
        ~ScriptSystem() override;
        [[nodiscard]] std::shared_ptr<const Script> running_script(Entity entity) const override;
        Result<void, Error> on_start(
            Scene& scene, RuntimeSession&, const RuntimeServices&) override;
        Result<void, Error> fixed_update(Scene& scene, const Context& context) override;
        Result<void, Error> update(Scene& scene, const Context& context) override;
        void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept override;

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
        Result<Entry, Error> prepare_entry(
            Entity entity, std::shared_ptr<const Script> script, ParameterMap overrides) const;
        struct ReloadGroup {
            std::map<AssetHandle, std::shared_ptr<const Script>> scripts;
            std::map<Key, Entity> instances;
        };
        struct FailedInstance {
            std::weak_ptr<const Script> script;
            ParameterMap overrides;
        };
        using FailedReload = std::map<Key, FailedInstance>;
        bool matches_failed_reload(const ReloadGroup& group, const FailedReload& failed) const;
        std::vector<ReloadGroup> reload_groups(const std::map<Key, Entity>& pending) const;
        Result<std::set<AssetHandle>, Error> reload_changed_scripts(
            const std::map<Key, Entity>& pending);
        Result<void, Error> install_reload(std::map<Key, Entry>& prepared);
        Result<void, Error> synchronize(Scene& scene);
        Result<void, Error> dispatch(Scene& scene, const Context& context, Script::Phase phase);
        Result<void, Error> invoke(const Key& key, Entry& entry, Script::Phase phase,
            const Context* context = nullptr, Entity contact_other = {},
            std::string_view event_handler = {}, const ParameterValue* event_value = nullptr);
        Result<void, Error> dispatch_contacts(Scene& scene, const Context& context);
        Result<void, Error> dispatch_events(Scene& scene, const Context& context);
        enum class StopReason { LiveChange, Shutdown };
        void stop_entry(Entry& entry, StopReason reason) noexcept;
        void stop_all() noexcept;

        ScriptAssets m_assets;
        const MaterialParameterValidator* m_materials;
        std::map<Key, Entry> m_entries;
        std::vector<Key> m_start_order;
        std::vector<FailedReload> m_failed_reloads;
        Scene* m_scene = nullptr;
        RuntimeSession* m_session = nullptr;
        AudioCommands* m_audio = nullptr;
        PhysicsCommands* m_physics = nullptr;
    };
}
