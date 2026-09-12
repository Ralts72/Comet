#include "editor_state.h"

namespace CometEditor {
    Comet::RenderView make_render_view(const EditorState& state, const bool visible,
        const Comet::Math::Vec2u render_size) {
        Comet::RenderView view{.visible = visible, .render_size = render_size};
        if(state.mode == EditorMode::Edit) {
            view.camera_selection = Comet::RenderView::CameraSelection::Override;
            view.camera_override = state.camera.snapshot();
        }
        return view;
    }
}
