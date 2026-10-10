#include "asset/registry.h"

#include "diagnostics/logger.h"

#include <atomic>

namespace Comet {
    namespace {
        uint64_t next_revision() noexcept {
            static std::atomic<uint64_t> revision{0};
            return revision.fetch_add(1, std::memory_order_relaxed) + 1;
        }
    }

    AssetRegistry::AssetRegistry() : m_revision(next_revision()) {}

    AssetRegistry::AssetRegistry(AssetRegistry&& other) noexcept
        : m_assets(std::move(other.m_assets)), m_revision(next_revision()) {
        other.clear();
    }

    AssetRegistry& AssetRegistry::operator=(AssetRegistry&& other) noexcept {
        if(this != &other) {
            m_assets = std::move(other.m_assets);
            m_revision = next_revision();
            other.clear();
        }
        return *this;
    }

    bool AssetRegistry::register_asset_impl(
        const AssetHandle handle, std::shared_ptr<void> asset, const std::type_index type) {
        if(!handle) {
            LOG_ERROR("Cannot register an asset with an invalid handle");
            return false;
        }

        if(!asset) {
            LOG_ERROR("Cannot register a null asset for handle {}", handle.value());
            return false;
        }

        const bool inserted =
            m_assets.emplace(handle, AssetEntry{.asset = std::move(asset), .type = type}).second;
        if(!inserted) {
            LOG_ERROR("Asset handle {} is already registered", handle.value());
            return false;
        }

        m_revision = next_revision();
        return true;
    }

    bool AssetRegistry::replace_asset_impl(
        const AssetHandle handle, std::shared_ptr<void> asset, const std::type_index type) {
        if(!handle) {
            LOG_ERROR("Cannot replace an asset with an invalid handle");
            return false;
        }
        if(!asset) {
            LOG_ERROR("Cannot replace an asset with null for handle {}", handle.value());
            return false;
        }

        const auto existing = m_assets.find(handle);
        if(existing == m_assets.end()) {
            LOG_ERROR("Cannot replace missing asset handle {}", handle.value());
            return false;
        }
        if(existing->second.type != type) {
            LOG_ERROR("Cannot replace asset handle {} with another runtime type", handle.value());
            return false;
        }

        existing->second.asset = std::move(asset);
        m_revision = next_revision();
        return true;
    }

    std::shared_ptr<void> AssetRegistry::resolve_impl(
        const AssetHandle handle, const std::type_index type) const {
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

    bool AssetRegistry::contains_impl(const AssetHandle handle, const std::type_index type) const {
        if(!handle)
            return false;
        const auto found = m_assets.find(handle);
        return found != m_assets.end() && found->second.type == type;
    }

    bool AssetRegistry::unregister_asset(const AssetHandle handle) {
        if(!handle || m_assets.erase(handle) == 0)
            return false;
        m_revision = next_revision();
        return true;
    }

    std::size_t AssetRegistry::size() const {
        return m_assets.size();
    }

    void AssetRegistry::clear() {
        m_assets.clear();
        m_revision = next_revision();
    }
}
