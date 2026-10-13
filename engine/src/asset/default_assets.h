#pragma once

#include "asset/metadata.h"

#include <array>
#include <string_view>

namespace Comet {
    // 固定身份不需要项目 .meta；项目通过普通资产引用替换默认资源。
    inline constexpr AssetHandle DEFAULT_CHARACTER_MESH{0xc04e700000000001ull};
    inline constexpr AssetHandle DEFAULT_PBR_MATERIAL{0xc04e700000000002ull};
    inline constexpr AssetHandle DEFAULT_UNLIT_MATERIAL{0xc04e700000000003ull};

    struct DefaultAsset {
        AssetHandle handle;
        AssetType type;
        std::string_view name;
        std::string_view material_template;
    };

    inline constexpr std::array DEFAULT_ASSETS{
        DefaultAsset{DEFAULT_CHARACTER_MESH, AssetType::Mesh, "引擎默认角色", {}},
        DefaultAsset{DEFAULT_PBR_MATERIAL, AssetType::Material, "引擎默认 PBR", "pbr"},
        DefaultAsset{DEFAULT_UNLIT_MATERIAL, AssetType::Material, "引擎默认无光照", "unlit_color"}};

    [[nodiscard]] constexpr const DefaultAsset* find_default_asset(const AssetHandle handle) {
        for(const auto& asset : DEFAULT_ASSETS) {
            if(asset.handle == handle)
                return &asset;
        }
        return nullptr;
    }
}
