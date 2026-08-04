#include "asset/registry.h"

#include "common/logger.h"

namespace Comet {
    bool AssetRegistry::register_asset_impl(
        const AssetHandle handle,
        std::shared_ptr<void> asset,
        const std::type_index type) {
        if(!handle) {
            LOG_ERROR("Cannot register an asset with an invalid handle");
            return false;
        }

        if(!asset) {
            LOG_ERROR("Cannot register a null asset for handle {}", handle.value());
            return false;
        }

        const bool inserted = m_assets.emplace(
            handle,
            AssetEntry{.asset = std::move(asset), .type = type}).second;
        if(!inserted) {
            LOG_ERROR("Asset handle {} is already registered", handle.value());
            return false;
        }

        return true;
    }

    std::shared_ptr<void> AssetRegistry::resolve_impl(
        const AssetHandle handle,
        const std::type_index type) const {
        if(!handle) {
            return nullptr;
        }

        const auto asset_it = m_assets.find(handle);
        if(asset_it == m_assets.end()) {
            return nullptr;
        }

        if(asset_it->second.type != type) {
            return nullptr;
        }

        return asset_it->second.asset;
    }

    bool AssetRegistry::contains(const AssetHandle handle) const {
        return handle && m_assets.contains(handle);
    }

    bool AssetRegistry::unregister_asset(const AssetHandle handle) {
        return handle && m_assets.erase(handle) > 0;
    }

    std::size_t AssetRegistry::size() const {
        return m_assets.size();
    }

    void AssetRegistry::clear() {
        m_assets.clear();
    }
}
