#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class Script;

    // 借用资产缓存，只提供不可变脚本定义；缓存须活到运行系统销毁之后。
    class COMET_API ScriptAssets final {
    public:
        explicit ScriptAssets(const AssetRegistry& assets) : m_assets(assets) {}
        [[nodiscard]] std::shared_ptr<const Script> resolve(AssetHandle handle) const;

    private:
        const AssetRegistry& m_assets;
    };
}
