#pragma once

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

using FileCommandCallback = std::function<void(FileCommand)>;

class MenuBar {
public:
  void render();

  void set_panel_visibility_callback(const std::string &panel_name,
                                     PanelVisibilityCallback callback);

  void set_file_command_callback(FileCommandCallback callback) {
    m_file_command_callback = std::move(callback);
  }

  [[nodiscard]] bool is_panel_visible(const std::string &panel_name) const;

  void set_fps(float fps) { m_fps = fps; }

private:
  void render_file_menu();
  void render_edit_menu();
  void render_view_menu();
  void render_gameobject_menu();
  void render_help_menu();

  std::map<std::string, bool> m_panel_visibility;
  std::map<std::string, PanelVisibilityCallback> m_panel_callbacks;
  FileCommandCallback m_file_command_callback;
  float m_fps = 0.0f;
};

} // namespace CometEditor
