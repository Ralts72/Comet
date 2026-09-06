#pragma once

#include "command_history.h"
#include "render/scene/render_scene.h"
#include "viewport_layout.h"
#include "core/geometry.h"

#include <array>
#include <optional>
#include <vector>

namespace CometEditor {
    class TransformGizmo {
    public:
        enum class Axis { X, Y, Z, All };
        enum class Space { World, Local };
        enum class Mode { Translate, Rotate, Scale };

        struct Settings {
            Mode mode = Mode::Translate;
            Space space = Space::World;
            bool snap = false;
            float translation_step = 0.25f;
            float rotation_step_degrees = 15.0f;
            float scale_step = 0.1f;
            bool operator==(const Settings&) const = default;
        };

        struct Segment {
            Comet::Math::Vec2 start;
            Comet::Math::Vec2 end;
        };

        struct Handle {
            Axis axis;
            std::vector<Segment> segments;
        };

        struct Input {
            Comet::Math::Vec2 position{};
            bool hovered = false;
            bool pressed = false;
            bool down = false;
            bool released = false;
            bool cancel = false;
        };

        TransformGizmo(CommandHistory& history, const Comet::ComponentRegistry& registry);
        [[nodiscard]] bool set_settings(Settings settings);
        [[nodiscard]] Settings settings() const { return m_settings; }

        // 坐标使用界面逻辑点；拖出图像仍继续，取消／释放帧也消费指针。
        [[nodiscard]] bool update(Comet::EntityUuid selected,
            const Comet::RenderCamera& camera, const ViewportLayout& layout,
            const Input& input);
        [[nodiscard]] bool cancel();
        [[nodiscard]] bool active() const;
        [[nodiscard]] std::optional<Axis> hovered_axis() const { return m_hovered_axis; }
        [[nodiscard]] std::optional<Axis> active_axis() const;
        [[nodiscard]] std::array<std::optional<Handle>, 4> handles(
            Comet::EntityUuid selected, const Comet::RenderCamera& camera,
            const ViewportLayout& layout) const;

    private:
        struct Context {
            Comet::EntityUuid parent;
            Comet::Math::Vec3 origin{};
            Comet::Math::Vec3 translation{};
            Comet::Math::Vec3 rotation{};
            Comet::Math::Vec3 scale{1.0f};
            std::array<Comet::Math::Vec3, 3> directions;
            Comet::Math::Mat3 rotation_frame{1.0f};
            Comet::Math::Mat3 inverse_rotation_frame{1.0f};
            Comet::Math::Mat4 parent_world{1.0f};
            Comet::Math::Mat4 world_to_parent{1.0f};
            Comet::Math::Mat4 view_projection{1.0f};
            Comet::Math::Mat4 inverse_view_projection{1.0f};
            ViewportLayout layout;
            float axis_length = 0.0f;
        };

        struct Drag {
            Comet::EntityUuid entity;
            std::uint64_t generation;
            Axis axis;
            Context context;
            float start_parameter;
            float last_parameter;
            float accumulated_angle = 0;
            Comet::Math::Vec3 last_value;
        };

        [[nodiscard]] std::optional<Context> make_context(Comet::EntityUuid selected,
            const Comet::RenderCamera& camera, const ViewportLayout& layout) const;
        [[nodiscard]] std::array<std::optional<Handle>, 4> make_handles(
            const Context& context) const;
        [[nodiscard]] static std::optional<Comet::Ray> pointer_ray(
            const Context& context, Comet::Math::Vec2 position);
        [[nodiscard]] static std::optional<float> axis_parameter(
            const Context& context, Axis axis, Comet::Math::Vec2 position);
        [[nodiscard]] static std::optional<float> rotation_parameter(
            const Context& context, Axis axis, Comet::Math::Vec2 position);
        [[nodiscard]] std::optional<float> parameter(
            const Context& context, Axis axis, Comet::Math::Vec2 position) const;
        [[nodiscard]] Comet::Math::Vec3 preview_value(Drag& drag, float parameter) const;

        CommandHistory& m_history;
        PropertyEditTransaction m_edit;
        std::optional<Drag> m_drag;
        std::optional<Axis> m_hovered_axis;
        Settings m_settings;
    };
}
