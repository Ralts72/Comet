#pragma once

#include "common/result.h"
#include "common/export.h"
#include "core/math_utils.h"
#include <array>
#include <filesystem>
#include <vector>

namespace Comet {
    // 开发源；发布时转为普通模型、纹理与网格产品，不在运行时生成地形。
    struct COMET_API TerrainData {
        struct Layer {
            Math::Vec3 color{1};
            std::string texture;
            float tiling = 8;
            std::string normal_texture, roughness_texture;
            float normal_scale = 1, roughness = 1;
            bool operator==(const Layer&) const = default;
        };
        struct Vegetation {
            uint32_t count = 512;
            uint32_t seed = 37;
            std::string model;
            float wind_amplitude = 0, wind_frequency = 1;
            bool flexible_wind = true;
            float min_height = .6f, max_height = 1.4f;
            float max_slope = 35;
            float min_elevation = 0, max_elevation = .8f;
            bool operator==(const Vegetation&) const = default;
        };
        uint32_t resolution = 65;
        std::string heightmap;
        std::vector<float> heights;
        // 与高度网格同序；空数组使用坡度／海拔规则，否则为归一化的三层权重。
        std::vector<Math::Vec3> layer_weights;
        float width = 64, depth = 64, height_scale = 12;
        uint32_t chunk_cells = 16;
        float skirt_depth = 2;
        bool collision = true;
        uint32_t albedo_resolution = 512;
        std::array<Layer, 3> layers{
            {{{.18f, .35f, .08f}, {}, 8}, {{.35f, .3f, .24f}, {}, 8}, {{.8f, .83f, .85f}, {}, 8}}};
        float slope_start = .15f, slope_end = .5f;
        float height_start = .6f, height_end = .85f;
        float roughness = .9f;
        Vegetation vegetation;
        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] static Result<TerrainData> deserialize(
            std::string_view, std::string_view source = {});
        [[nodiscard]] static Result<TerrainData> load(const std::filesystem::path&);
        [[nodiscard]] Result<std::string> serialize() const;
        [[nodiscard]] Result<void> save(const std::filesystem::path&) const;
        bool operator==(const TerrainData&) const = default;
    };
}
