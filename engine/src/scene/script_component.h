#pragma once

#include "scene/property.h"
#include <memory>

namespace Comet {
    class Script;
    class ScriptSystem;
    struct COMET_API ScriptComponent {
        ScriptComponent();
        ScriptComponent(const ScriptComponent& other);
        ScriptComponent& operator=(const ScriptComponent& other);
        ScriptComponent(ScriptComponent&&) noexcept = default;
        ScriptComponent& operator=(ScriptComponent&&) noexcept = default;

        AssetHandle asset;
        ParameterMap parameters;
        [[nodiscard]] uint64_t lifetime() const { return m_lifetime; }
        [[nodiscard]] std::shared_ptr<const Script> running_script() const {
            return m_running_script.lock();
        }

    private:
        friend class ScriptSystem;
        // 仅借用活动实例的定义，复制／序列化不携带运行绑定。
        std::weak_ptr<const Script> m_running_script;
        // 复制得到新寿命，EnTT 存储搬移保留寿命；不序列化。
        uint64_t m_lifetime;
    };
}
