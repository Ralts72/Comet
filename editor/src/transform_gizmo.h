#pragma once

#include "editor_state.h"
#include "core/geometry.h"
#include "render/debug/debug_draw.h"
#include "render/scene/render_types.h"
#include "scene/entity_uuid.h"

#include <optional>

namespace CometEditor {
    enum class TransformGizmoAxis {
        X,
        Y,
        Z
    };

    struct TranslationGizmoFrame {
        Comet::Math::Vec3 origin{};
        float axis_length = 0.0f;
        Comet::ViewProjectMatrix view_project;
        Comet::Math::Vec2u render_resolution{};
    };

    struct TranslationGizmoDrag {
        TransformGizmoAxis axis = TransformGizmoAxis::X;
        Comet::Math::Vec3 world_origin{};
        Comet::Math::Mat4 world_to_parent = Comet::Math::Mat4(1.0f);
        float start_axis_parameter = 0.0f;
    };

    struct TranslationGizmoContext {
        Comet::EntityUuid entity_uuid;
        Comet::Math::Vec3 local_translation{};
        Comet::Math::Mat4 world_to_parent = Comet::Math::Mat4(1.0f);
        TranslationGizmoFrame frame;
        float hit_radius_pixels = 0.0f;
    };

    struct TranslationGizmoPointerInput {
        Comet::Math::Vec2 pixel_position{};
        bool pointer_over_image = false;
        bool pressed = false;
        bool down = false;
        bool released = false;
    };

    struct TranslationGizmoEdit {
        Comet::EntityUuid entity_uuid;
        Comet::Math::Vec3 translation{};
    };

    struct TranslationGizmoCommit {
        Comet::EntityUuid entity_uuid;
        Comet::Math::Vec3 before{};
        Comet::Math::Vec3 after{};
    };

    struct TranslationGizmoUpdate {
        std::optional<TranslationGizmoEdit> edit;
        std::optional<TranslationGizmoCommit> commit;
        bool pointer_press_consumed = false;
    };

    class TranslationGizmoController {
    public:
        [[nodiscard]] TranslationGizmoUpdate update(
            const std::optional<TranslationGizmoContext>& context,
            const std::optional<TranslationGizmoPointerInput>& input);

        [[nodiscard]] std::optional<TranslationGizmoEdit> cancel();
        void reset();

        [[nodiscard]] std::optional<TransformGizmoAxis> hovered_axis() const {
            return m_hovered_axis;
        }
        [[nodiscard]] std::optional<TransformGizmoAxis> active_axis() const {
            return m_active
                ? std::optional<TransformGizmoAxis>(m_active->drag.axis)
                : std::nullopt;
        }

    private:
        struct ActiveDrag {
            Comet::EntityUuid entity_uuid;
            Comet::Math::Vec3 before{};
            TranslationGizmoDrag drag;
        };

        std::optional<ActiveDrag> m_active;
        std::optional<TransformGizmoAxis> m_hovered_axis;
    };

    [[nodiscard]] Comet::Math::Vec3 transform_gizmo_axis_direction(
        TransformGizmoAxis axis);

    [[nodiscard]] std::optional<TranslationGizmoFrame>
    make_translation_gizmo_frame(
        const EditorCameraState& camera,
        Comet::Math::Vec3 world_origin,
        Comet::Math::Vec2u render_resolution,
        float axis_length_pixels);

    [[nodiscard]] std::optional<Comet::Math::Vec2>
    project_translation_gizmo_point(
        const TranslationGizmoFrame& frame,
        Comet::Math::Vec3 world_point);

    [[nodiscard]] std::optional<Comet::Ray>
    make_translation_gizmo_ray(
        const TranslationGizmoFrame& frame,
        Comet::Math::Vec2 pixel_position);

    [[nodiscard]] std::optional<TransformGizmoAxis>
    pick_translation_gizmo_axis(
        const TranslationGizmoFrame& frame,
        Comet::Math::Vec2 pixel_position,
        float hit_radius_pixels);

    [[nodiscard]] std::optional<TranslationGizmoDrag>
    begin_translation_gizmo_drag(
        const TranslationGizmoFrame& frame,
        TransformGizmoAxis axis,
        Comet::Math::Vec2 pixel_position,
        const Comet::Math::Mat4& world_to_parent);

    [[nodiscard]] std::optional<Comet::Math::Vec3>
    update_translation_gizmo_drag(
        const TranslationGizmoDrag& drag,
        const Comet::Ray& pointer_ray);

    [[nodiscard]] bool add_translation_gizmo(
        Comet::DebugDrawList& draw_list,
        const TranslationGizmoFrame& frame,
        std::optional<TransformGizmoAxis> hovered_axis,
        std::optional<TransformGizmoAxis> active_axis);
}
