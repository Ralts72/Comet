#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <cstddef>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Comet {
    class COMET_API AssetRegistry {
    public:
        AssetRegistry() = default;

        ~AssetRegistry() = default;

        AssetRegistry(const AssetRegistry&) = delete;

        AssetRegistry& operator=(const AssetRegistry&) = delete;

        AssetRegistry(AssetRegistry&&) noexcept = default;

        AssetRegistry& operator=(AssetRegistry&&) noexcept = default;

        template<typename T>
        [[nodiscard]] bool register_asset(AssetHandle handle, std::shared_ptr<T> asset);

        template<typename T>
        [[nodiscard]] std::shared_ptr<T> resolve(AssetHandle handle) const;

        [[nodiscard]] bool contains(AssetHandle handle) const;

        [[nodiscard]] bool unregister_asset(AssetHandle handle);

        [[nodiscard]] std::size_t size() const;

        void clear();

    private:
        struct AssetEntry {
            std::shared_ptr<void> asset;
            std::type_index type;
        };

        [[nodiscard]] bool register_asset_impl(
            AssetHandle handle,
            std::shared_ptr<void> asset,
            std::type_index type);

        [[nodiscard]] std::shared_ptr<void> resolve_impl(
            AssetHandle handle,
            std::type_index type) const;

        std::unordered_map<AssetHandle, AssetEntry> m_assets;
    };

    template<typename T>
    bool AssetRegistry::register_asset(const AssetHandle handle, std::shared_ptr<T> asset) {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return register_asset_impl(
            handle,
            std::shared_ptr<void>(std::move(asset)),
            std::type_index(typeid(T)));
    }

    template<typename T>
    std::shared_ptr<T> AssetRegistry::resolve(const AssetHandle handle) const {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        const auto asset = resolve_impl(
            handle,
            std::type_index(typeid(std::remove_cv_t<T>)));
        return std::static_pointer_cast<T>(asset);
    }
}
