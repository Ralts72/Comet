#include "editor_camera_controller.h"

#include <algorithm>
#include <cmath>

namespace CometEditor {
    namespace {
        constexpr float ORBIT_RADIANS_PER_PIXEL = 0.005f;
        constexpr float ZOOM_EXPONENT_PER_STEP = 0.15f;
        constexpr float MIN_DISTANCE = 0.05f;
        constexpr float MAX_DISTANCE = 10000.0f;
        constexpr float MAX_VERTICAL_ALIGNMENT = 0.995f;
        constexpr float MIN_DIRECTION_LENGTH = 0.00001f;

        bool is_finite(const Comet::Math::Vec2 value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool has_delta(const Comet::Math::Vec2 value) {
            return value.x != 0.0f || value.y != 0.0f;
        }
    }

    bool apply_editor_camera_input(
        EditorCameraState& camera,
        const EditorCameraInput& input) {
        if(!is_finite(input.orbit_delta)
           || !is_finite(input.pan_delta)
           || !std::isfinite(input.zoom_delta)) {
            return false;
        }

        Comet::Math::Vec3 offset = camera.position - camera.target;
        float distance = Comet::Math::length(offset);
        if(!std::isfinite(distance) || distance < MIN_DIRECTION_LENGTH) {
            return false;
        }

        const Comet::Math::Vec3 world_up(0.0f, 1.0f, 0.0f);
        bool changed = false;

        if(has_delta(input.orbit_delta)) {
            offset = Comet::Math::angle_axis(
                -input.orbit_delta.x * ORBIT_RADIANS_PER_PIXEL,
                world_up) * offset;

            const Comet::Math::Vec3 forward =
                -Comet::Math::normalize(offset);
            const Comet::Math::Vec3 right_candidate =
                Comet::Math::cross(forward, world_up);
            if(Comet::Math::length(right_candidate)
               >= MIN_DIRECTION_LENGTH) {
                const Comet::Math::Vec3 right =
                    Comet::Math::normalize(right_candidate);
                const Comet::Math::Vec3 pitched =
                    Comet::Math::angle_axis(
                        input.orbit_delta.y * ORBIT_RADIANS_PER_PIXEL,
                        right) * offset;
                const Comet::Math::Vec3 pitched_forward =
                    -Comet::Math::normalize(pitched);
                if(std::abs(Comet::Math::dot(
                       pitched_forward, world_up))
                   < MAX_VERTICAL_ALIGNMENT) {
                    offset = pitched;
                }
            }
            camera.position = camera.target + offset;
            camera.up = world_up;
            distance = Comet::Math::length(offset);
            changed = true;
        }

        if(has_delta(input.pan_delta)
           && std::isfinite(input.viewport_height)
           && input.viewport_height > 0.0f
           && std::isfinite(camera.fov_degrees)
           && camera.fov_degrees > 0.0f
           && camera.fov_degrees < 179.0f) {
            const Comet::Math::Vec3 forward =
                Comet::Math::normalize(camera.target - camera.position);
            const Comet::Math::Vec3 right_candidate =
                Comet::Math::cross(forward, world_up);
            if(Comet::Math::length(right_candidate)
               >= MIN_DIRECTION_LENGTH) {
                const Comet::Math::Vec3 right =
                    Comet::Math::normalize(right_candidate);
                const Comet::Math::Vec3 camera_up =
                    Comet::Math::normalize(
                        Comet::Math::cross(right, forward));
                const float visible_world_height =
                    2.0f * distance * std::tan(
                        Comet::Math::radians(camera.fov_degrees) * 0.5f);
                const float world_units_per_pixel =
                    visible_world_height / input.viewport_height;
                const Comet::Math::Vec3 translation =
                    right * (-input.pan_delta.x * world_units_per_pixel)
                    + camera_up
                        * (input.pan_delta.y * world_units_per_pixel);
                camera.position += translation;
                camera.target += translation;
                changed = true;
            }
        }

        if(input.zoom_delta != 0.0f) {
            const Comet::Math::Vec3 direction =
                Comet::Math::normalize(camera.position - camera.target);
            distance = std::clamp(
                distance * std::exp(
                    -input.zoom_delta * ZOOM_EXPONENT_PER_STEP),
                MIN_DISTANCE,
                MAX_DISTANCE);
            camera.position = camera.target + direction * distance;
            changed = true;
        }

        return changed;
    }
}
