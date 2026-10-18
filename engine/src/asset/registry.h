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
#include <vector>

namespace Comet {
    class COMET_API AssetRegistry {
    public:
        struct Removals {
            bool all = false;
            // take_removals 生成有序且不重复的身份；批次不借用注册表或资源。
            std::vector<AssetHandle> handles;
            [[nodiscard]] bool empty() const { return !all && handles.empty(); }
            [[nodiscard]] bool contains(AssetHandle handle) const;
        };

        AssetRegistry();

        ~AssetRegistry() = default;

        AssetRegistry(const AssetRegistry&) = delete;

        AssetRegistry& operator=(const AssetRegistry&) = delete;

        AssetRegistry(AssetRegistry&& other) noexcept;

        AssetRegistry& operator=(AssetRegistry&& other) noexcept;

        // 字节来自创建工厂；注册表不解引用资源，未知／非 GPU 资产为 0。
        template<typename T>
        [[nodiscard]] bool register_asset(
            AssetHandle handle, std::shared_ptr<T> asset, uint64_t gpu_bytes = 0);

        template<typename T>
        [[nodiscard]] bool replace_asset(
            AssetHandle handle, std::shared_ptr<T> asset, uint64_t gpu_bytes = 0);

        // 可选返回同一资源的发布版本；缺失或类型不匹配时为 0。
        template<typename T>
        [[nodiscard]] std::shared_ptr<T> resolve(
            AssetHandle handle, uint64_t* revision = nullptr) const;

        [[nodiscard]] bool contains(AssetHandle handle) const;
        template<typename T> [[nodiscard]] bool contains(AssetHandle handle) const;

        [[nodiscard]] bool unregister_asset(AssetHandle handle);

        [[nodiscard]] std::size_t size() const;
        // 已注册 GPU 资产的分配字节；在途旧代和渲染目标另由 GPU 诊断观测。
        [[nodiscard]] uint64_t resident_gpu_bytes() const;

        void clear();

        // Main 取走一次并交付所有消费者；同类型重新注册抵消尚未交付的删除请求。
        [[nodiscard]] Removals take_removals();

        // 成功发布或移除资源后改变；不同注册表不会共用版本。
        [[nodiscard]] uint64_t get_revision() const noexcept { return m_revision; }
        // 当前资源的发布版本；未注册的 Handle 返回 0。
        [[nodiscard]] uint64_t get_revision(AssetHandle handle) const;

    private:
        struct AssetEntry {
            std::shared_ptr<void> asset;
            std::type_index type;
            uint64_t revision;
            uint64_t gpu_bytes = 0;
        };

        [[nodiscard]] bool register_asset_impl(AssetHandle handle, std::shared_ptr<void> asset,
            std::type_index type, uint64_t gpu_bytes);

        [[nodiscard]] bool replace_asset_impl(AssetHandle handle, std::shared_ptr<void> asset,
            std::type_index type, uint64_t gpu_bytes);

        [[nodiscard]] std::shared_ptr<void> resolve_impl(
            AssetHandle handle, std::type_index type, uint64_t* revision) const;
        [[nodiscard]] bool contains_impl(AssetHandle handle, std::type_index type) const;

        // 标记始终是完整类型；T 可前置声明，类型身份不受各编译单元的完整性影响。
        // 跨共享库的资源前置声明须保留导出属性。
        template<typename T> struct AssetType {};
        template<typename T> static std::type_index asset_type() {
            return std::type_index(typeid(AssetType<std::remove_cv_t<T>>));
        }

        std::unordered_map<AssetHandle, AssetEntry> m_assets;
        uint64_t m_revision;
        std::vector<std::pair<AssetHandle, std::type_index>> m_removed;
        bool m_removed_all = false;
    };

    template<typename T>
    bool AssetRegistry::register_asset(
        const AssetHandle handle, std::shared_ptr<T> asset, uint64_t bytes) {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return register_asset_impl(
            handle, std::shared_ptr<void>(std::move(asset)), asset_type<T>(), bytes);
    }

    template<typename T>
    bool AssetRegistry::replace_asset(
        const AssetHandle handle, std::shared_ptr<T> asset, uint64_t bytes) {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return replace_asset_impl(
            handle, std::shared_ptr<void>(std::move(asset)), asset_type<T>(), bytes);
    }

    template<typename T>
    std::shared_ptr<T> AssetRegistry::resolve(const AssetHandle handle, uint64_t* revision) const {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");

        return std::static_pointer_cast<T>(resolve_impl(handle, asset_type<T>(), revision));
    }

    template<typename T> bool AssetRegistry::contains(const AssetHandle handle) const {
        static_assert(!std::is_void_v<T>, "AssetRegistry requires a concrete asset type");
        return contains_impl(handle, asset_type<T>());
    }
}
