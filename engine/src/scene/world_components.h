#pragma once

#include "core/math_utils.h"
#include "scene/entity_uuid.h"
#include "asset/handle.h"
#include <string>

namespace Comet {
    struct SplineComponent {
        Math::Vec3 start{0}, control_a{0, 0, -3}, control_b{3, 0, -3}, end{3, 0, 0};
        [[nodiscard]] Math::Vec3 sample(float t) const {
            const float u = 1 - t;
            return start * (u * u * u) + control_a * (3 * u * u * t) + control_b * (3 * u * t * t)
                   + end * (t * t * t);
        }
        [[nodiscard]] Math::Vec3 tangent(float t) const {
            const float u = 1 - t;
            return 3 * u * u * (control_a - start) + 6 * u * t * (control_b - control_a)
                   + 3 * t * t * (end - control_b);
        }
        bool operator==(const SplineComponent&) const = default;
    };
    struct SplineFollowerComponent {
        EntityUuid path;
        bool playing = true;
        float duration = 5;
        bool loop = false;
        bool orient = true;
        bool ease = true;
        std::string skip_action = "game.skip_cutscene";
        std::string finished_event = "cutscene.finished";
        bool operator==(const SplineFollowerComponent&) const = default;
    };
    struct WeatherComponent {
        bool enabled = true;
        float hour = 14;
        float day_length = 0; // 真实秒；0 固定时刻。
        float overcast = .2f;
        Math::Vec3 wind{2, 0, 0};
        float fog_density = .015f;
        Math::Vec3 fog_color{.65f, .7f, .75f};
        EntityUuid sun;
        float sun_intensity = 3;
        float sun_yaw = 45;
        EntityUuid precipitation;
        float precipitation_amount = 0;
        bool operator==(const WeatherComponent&) const = default;
    };
    struct StreamCellComponent {
        bool enabled = true;
        AssetHandle prefab;
        float load_distance = 40, unload_distance = 55;
        bool operator==(const StreamCellComponent&) const = default;
    };
    struct VegetationInteractorComponent {
        float radius = .5f, height = 1.8f;
        bool operator==(const VegetationInteractorComponent&) const = default;
    };
    struct WaterSurfaceComponent {
        bool enabled = true;
        float amplitude = .08f;
        float wavelength = 4;
        float speed = 1;
        bool operator==(const WaterSurfaceComponent&) const = default;
    };
}
