#pragma once

#include "command_history.h"
#include "render/scene/render_scene.h"
#include "viewport_layout.h"

#include <array>
#include <optional>

namespace CometEditor {
    class TranslationGizmo {
    public:
        enum class Axis { X, Y, Z };

        struct Handle {
            Axis axis;
            Comet::Math::Vec2 start;
            Comet::Math::Vec2 end;
        };

        struct Input {
            Comet::Math::Vec2 position{};
            bool hovered = false;
            bool pressed = false;
            bool down = false;
            bool released = false;
            bool cancel = false;
        };

        TranslationGizmo(
            CommandHistory& history, const Comet::ComponentRegistry& registry);

        // 坐标使用界面逻辑点；拖出图像仍继续，取消／释放帧也消费指针。
        [[nodiscard]] bool update(Comet::EntityUuid selected,
            const Comet::RenderCamera& camera, const ViewportLayout& layout,
            const Input& input);
        [[nodiscard]] bool cancel();
        [[nodiscard]] bool active() const;
        [[nodiscard]] std::optional<Axis> hovered_axis() const { return m_hovered_axis; }
        [[nodiscard]] std::optional<Axis> active_axis() const;
        [[nodiscard]] std::array<std::optional<Handle>, 3> handles(
            Comet::EntityUuid selected, const Comet::RenderCamera& camera,
            const ViewportLayout& layout) const;

    private:
        struct Context {
            Comet::EntityUuid parent;
            Comet::Math::Vec3 origin{};
            Comet::Math::Vec3 translation{};
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
        };

        [[nodiscard]] std::optional<Context> make_context(Comet::EntityUuid selected,
            const Comet::RenderCamera& camera, const ViewportLayout& layout) const;
        [[nodiscard]] static std::array<std::optional<Handle>, 3> make_handles(
            const Context& context);
        [[nodiscard]] static std::optional<float> axis_parameter(
            const Context& context, Axis axis, Comet::Math::Vec2 position);

        CommandHistory& m_history;
        PropertyEditTransaction m_edit;
        std::optional<Drag> m_drag;
        std::optional<Axis> m_hovered_axis;
    };
}
