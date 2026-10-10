#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Comet {
    class COMET_API AssetRegistry {
    public:
        AssetRegistry();

        ~AssetRegistry() = default;

        AssetRegistry(const AssetRegistry&) = delete;

        AssetRegistry& operator=(const AssetRegistry&) = delete;

        AssetRegistry(AssetRegistry&& other) noexcept;

        AssetRegistry& operator=(AssetRegistry&& other) noexcept;

        template<typename T>
        [[nodiscard]] bool register_asset(AssetHandle handle, std::shared_ptr<T> asset);

        template<typename T>
        [[nodiscard]] bool replace_asset(AssetHandle handle, std::shared_ptr<T> asset);

        // 可选返回同一资源的发布版本；缺失或类型不匹配时为 0。
        template<typename T>
        [[nodiscard]] std::shared_ptr<T> resolve(
            AssetHandle handle, uint64_t* revision = nullptr) const;

        [[nodiscard]] bool contains(AssetHandle handle) const;
        template<typename T> [[nodiscard]] bool contains(AssetHandle handle) const;

        [[nodiscard]] bool unregister_asset(AssetHandle handle);

        [[nodiscard]] std::size_t size() const;

        void clear();

        // 成功发布或移除资源后改变；不同注册表不会共用版本。
        [[nodiscard]] uint64_t get_revision() const noexcept { return m_revision; }
        // 当前资源的发布版本；未注册的 Handle 返回 0。
        [[nodiscard]] uint64_t get_revision(AssetHandle handle) const;

    private:
        struct AssetEntry {
            std::shared_ptr<void> asset;
            std::type_index type;
            uint64_t revision;
        };

        [[nodiscard]] bool register_asset_impl(
            AssetHandle handle, std::shared_ptr<void> asset, std::type_index type);

        [[nodiscard]] bool replace_asset_impl(
            AssetHandle handle, std::shared_ptr<void> asset, std::type_index type);

        [[nodiscard]] std::shared_ptr<void> resolve_impl(
            AssetHandle handle, std::type_index type, uint64_t* revision) const;
        [[nodiscard]] bool contains_impl(AssetHandle handle, std::type_index type) const;

        std::unordered_map<AssetHandle, AssetEntry> m_assets;
        uint64_t m_revision;
    };

    template<typename T>
    bool AssetRegistry::register_asset(const AssetHandle handle, std::shared_ptr<T> asset) {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return register_asset_impl(
            handle, std::shared_ptr<void>(std::move(asset)), std::type_index(typeid(T)));
    }

    template<typename T>
    bool AssetRegistry::replace_asset(const AssetHandle handle, std::shared_ptr<T> asset) {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return replace_asset_impl(
            handle, std::shared_ptr<void>(std::move(asset)), std::type_index(typeid(T)));
    }

    template<typename T>
    std::shared_ptr<T> AssetRegistry::resolve(const AssetHandle handle, uint64_t* revision) const {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return std::static_pointer_cast<T>(
            resolve_impl(handle, std::type_index(typeid(std::remove_cv_t<T>)), revision));
    }

    template<typename T> bool AssetRegistry::contains(const AssetHandle handle) const {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");
        return contains_impl(handle, std::type_index(typeid(std::remove_cv_t<T>)));
    }
}
