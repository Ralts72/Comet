#include "scripting/script_assets.h"
#include "scripting/script.h"
#include "asset/registry.h"

namespace Comet {
    std::shared_ptr<const Script> ScriptAssets::resolve(const AssetHandle handle) const {
        return m_assets.resolve<Script>(handle);
    }
}
