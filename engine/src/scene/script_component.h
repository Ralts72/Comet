#pragma once

#include "scene/property.h"

namespace Comet {
    struct COMET_API ScriptComponent {
        ScriptComponent();
        ScriptComponent(const ScriptComponent& other);
        ScriptComponent& operator=(const ScriptComponent& other);
        ScriptComponent(ScriptComponent&&) noexcept = default;
        ScriptComponent& operator=(ScriptComponent&&) noexcept = default;

        AssetHandle asset;
        ParameterMap parameters;
        [[nodiscard]] uint64_t lifetime() const { return m_lifetime; }

    private:
        // 复制得到新寿命，EnTT 存储搬移保留寿命；不序列化。
        uint64_t m_lifetime;
    };
}
