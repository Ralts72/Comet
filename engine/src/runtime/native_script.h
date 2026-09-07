#pragma once

#include "runtime/scene_runtime.h"
#include "scene/component_registry.h"

#include <map>

namespace Comet {
    // 作者数据的瞬态生命周期标识；复制产生新身份，EnTT 搬移保留身份。
    class COMET_API ScriptComponent {
    public:
        ScriptComponent();
        ScriptComponent(const ScriptComponent&);
        ScriptComponent& operator=(const ScriptComponent&);
        ScriptComponent(ScriptComponent&&) noexcept = default;
        ScriptComponent& operator=(ScriptComponent&&) noexcept = default;
        [[nodiscard]] uint64_t get_instance_key() const { return m_instance_key; }

    private:
        uint64_t m_instance_key;
    };

    class COMET_API NativeScript {
    public:
        virtual ~NativeScript() = default;
        virtual void on_start(Scene&, Entity) {}
        virtual void fixed_update(Scene&, Entity, const System::Context&) {}
        virtual void update(Scene&, Entity, const System::Context&) {}
        // 实体／组件可能已经删除；清理不能依赖其地址，也须容忍部分启动。
        virtual void on_stop() noexcept {}
    };

    class COMET_API NativeScriptSystem final: public System {
    public:
        explicit NativeScriptSystem(const ComponentRegistry& registry);
        ~NativeScriptSystem() override;
        void on_start(Scene& scene) override;
        void fixed_update(Scene& scene, const Context& context) override;
        void update(Scene& scene, const Context& context) override;
        void on_stop(Scene&) noexcept override;

    private:
        struct Key {
            size_t type;
            EntityUuid entity;
            uint64_t instance;
            auto operator<=>(const Key&) const = default;
        };
        struct Instance {
            Entity entity;
            std::unique_ptr<NativeScript> script;
        };
        [[nodiscard]] bool is_live(const Key& key, const Instance& instance) const;
        void synchronize(Scene& scene);
        void stop_missing();
        void stop_all() noexcept;
        void dispatch(Scene& scene, const Context& context, bool fixed);

        std::vector<ComponentDescriptor> m_types;
        std::map<Key, Instance> m_instances;
    };

    template<typename Component, typename Script>
        requires(std::is_base_of_v<ScriptComponent, Component>
                 && std::is_base_of_v<NativeScript, Script>)
    ComponentDescriptor make_script_descriptor(std::string id, std::string display_name,
        std::vector<PropertyDescriptor> properties) {
        auto descriptor = make_component_descriptor<Component>(
            std::move(id), std::move(display_name), std::move(properties));
        descriptor.create_script = [] { return std::make_unique<Script>(); };
        descriptor.script_instance_key = [](const Entity& entity) {
            return entity.get_component<Component>().get_instance_key();
        };
        return descriptor;
    }
}
