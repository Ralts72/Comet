#pragma once

namespace CometEditor {
    enum class EditorMode { Edit, Play };

    struct EditorState {
        EditorMode mode = EditorMode::Edit;
    };
}
