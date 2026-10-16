#pragma once

#include "asset/data/irradiance_data.h"
#include "asset/data/material_data.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "render/lighting.h"

#include <functional>
#include <memory>
#include <vector>

namespace Comet::IrradianceBake {
    inline constexpr uint32_t VERSION = 1;

    struct Material {
        Math::Vec4 base_color{1};
        Math::Vec3 emissive{};
        float metallic = 0;
        MaterialSurface surface;
        std::shared_ptr<const TextureData> base_color_texture;
        std::shared_ptr<const TextureData> metallic_roughness_texture;
        std::shared_ptr<const TextureData> emissive_texture;
    };

    struct Object {
        std::shared_ptr<const MeshData> mesh;
        Math::Mat4 transform{1};
        std::uint32_t material = 0;
    };

    struct Input {
        std::vector<Object> objects;
        std::vector<Material> materials;
        std::vector<RenderLight> lights;
        std::shared_ptr<const TextureData> environment;
        float environment_intensity = 1;
        float environment_rotation = 0; // 与 IBL 一致，单位为度。
    };

    struct Settings {
        BoundingBox bounds{{-5, 0, -5}, {5, 5, 5}};
        float spacing = 1;
        float max_distance = 100;
        std::uint32_t ray_samples = 512;
        std::uint32_t bounces = 2;
        std::uint32_t seed = 0;
    };

    [[nodiscard]] Result<Math::Vec3u> grid_dimensions(const Settings& settings);

    struct Statistics {
        std::uint64_t rays = 0;
        std::uint32_t triangles = 0;
        std::uint32_t bvh_nodes = 0;
        std::uint32_t active_probes = 0;
        std::size_t working_bytes = 0; // 烘焙器自有数据；不含输入快照或进程工作集。
        double seconds = 0;
    };

    struct Output {
        IrradianceData data;
        Statistics statistics;
    };

    // 返回 false 取消；调用发生在烘焙线程，参数为已完成／全部探针数。
    using Progress = std::function<bool(std::uint32_t, std::uint32_t)>;
    [[nodiscard]] Result<Output> bake(
        const Input& input, const Settings& settings, const Progress& progress = {});
}
