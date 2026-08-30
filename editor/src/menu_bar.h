#pragma once

#include "editor_state.h"

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace CometEditor {

using PanelVisibilityCallback = std::function<void(bool)>;

enum class FileCommand {
  NewScene,
  OpenScene,
  SaveScene
};

enum class EditCommand {
  Undo,
  Redo
};

using FileCommandCallback = std::function<void(FileCommand)>;
using EditCommandCallback = std::function<void(EditCommand)>;
using EditorModeCallback = std::function<void(EditorMode)>;

class MenuBar {
public:
  explicit MenuBar(const EditorState& state) : m_state(state) {}

  void render();

  void set_panel_visibility_callback(const std::string &panel_name,
                                     PanelVisibilityCallback callback);

  void set_file_command_callback(FileCommandCallback callback) {
    m_file_command_callback = std::move(callback);
  }

  void set_editor_mode_callback(EditorModeCallback callback) {
    m_editor_mode_callback = std::move(callback);
  }

  void set_edit_command_callback(EditCommandCallback callback) {
    m_edit_command_callback = std::move(callback);
  }

  void set_edit_history_state(const bool can_undo, const bool can_redo) {
    m_can_undo = can_undo;
    m_can_redo = can_redo;
  }

  [[nodiscard]] bool is_panel_visible(const std::string &panel_name) const;

  void set_fps(const float fps) { m_fps = fps; }

private:
  void render_file_menu() const;
  void render_edit_menu();
  void render_edit_shortcuts();
  void render_view_menu();
  void render_game_menu() const;
  void render_gameobject_menu();
  void render_help_menu();

  const EditorState& m_state;
  std::map<std::string, bool> m_panel_visibility;
  std::map<std::string, PanelVisibilityCallback> m_panel_callbacks;
  FileCommandCallback m_file_command_callback;
  EditCommandCallback m_edit_command_callback;
  EditorModeCallback m_editor_mode_callback;
  bool m_can_undo = false;
  bool m_can_redo = false;
  float m_fps = 0.0f;
};

} // namespace CometEditor
