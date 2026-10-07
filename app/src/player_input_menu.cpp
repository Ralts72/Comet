#include "player_input_menu.h"

#include "core/window.h"
#include "diagnostics/logger.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace CometApp {
    namespace {
        constexpr const char* required_elements[]{"hud", "fps", "settings", "notice", "menu",
            "panel", "action-selector", "action-name", "previous", "next", "bindings", "status",
            "error", "footer", "restore", "cancel", "apply"};
        std::string escape_rml(const std::string_view value) {
            std::string escaped;
            escaped.reserve(value.size());
            for(const char character : value) {
                switch(character) {
                    case '&':
                        escaped += "&amp;";
                        break;
                    case '<':
                        escaped += "&lt;";
                        break;
                    case '>':
                        escaped += "&gt;";
                        break;
                    case '"':
                        escaped += "&quot;";
                        break;
                    case '\'':
                        escaped += "&#39;";
                        break;
                    default:
                        escaped += character;
                        break;
                }
            }
            return escaped;
        }

    }

    class PlayerInputMenu::Impl final {
    public:
        Impl(Comet::Window& window, std::unique_ptr<Comet::Ui::RmlContext> runtime)
            : m_window(window), m_runtime(std::move(runtime)), m_context(&m_runtime->context()) {}
        ~Impl() {
            m_document = nullptr;
            m_runtime.reset();
        }
        Comet::Result<void> initialize() {
            using Initialization = Comet::Result<void>;
            auto constructor = m_context->CreateDataModel("runtime");
            if(!constructor || !constructor.Bind("fps_text", &m_fps_text)
                || !constructor.Bind("action_name", &m_action_name)
                || !constructor.Bind("status_text", &m_status_text)
                || !constructor.Bind("error_text", &m_error_text)
                || !constructor.Bind("menu_available", &m_menu_available)
                || !constructor.Bind("waiting", &m_waiting)
                || !constructor.Bind("has_actions", &m_has_actions)
                || !constructor.BindEventCallback(
                    "command", [this](Rml::DataModelHandle, Rml::Event& event,
                                   const Rml::VariantList& arguments) {
                        const auto* target = event.GetTargetElement();
                        if(m_document && target && !m_runtime->is_loading_document()
                            && target->GetOwnerDocument() == m_document)
                            command(arguments);
                    }))
                return Initialization::failure("Cannot bind runtime UI view model");
            m_model = constructor.GetModelHandle();
            return reload();
        }

        Comet::Result<void> reload() {
            auto candidate = m_runtime->replace_document(m_document, "ui/runtime.rml",
                [](Rml::ElementDocument& loaded) -> Comet::Result<void> {
                    for(const auto* id : required_elements) {
                        if(!loaded.GetElementById(id))
                            return Comet::Result<void>::failure(
                                "界面模板缺少必需元素：" + std::string(id));
                    }
                    loaded.GetElementById("menu")->SetProperty("display", "block");
                    return Comet::Result<void>::success();
                });
            if(!candidate) {
                m_view_error = candidate.error();
                sync();
                (void)m_runtime->update();
                return Comet::Result<void>::failure(candidate.error());
            }
            m_document = candidate.value();
            m_menu_displayed = !m_open;
            m_view_error.clear();
            m_edit.cancel_capture();
            m_pending_capture.reset();
            m_capture_focus.clear();
            m_runtime->cancel_input();
            m_rows_dirty = true;
            sync();
            if(auto updated = m_runtime->update(); !updated)
                return updated;
            if(m_open)
                focus_first_binding();
            return Comet::Result<void>::success();
        }

        void command(const Rml::VariantList& arguments) {
            if(arguments.empty())
                return;
            const auto operation = arguments[0].Get<Rml::String>();
            if(operation == "open") {
                if(m_menu_available && !m_open) {
                    m_open_requested = true;
                    m_runtime->cancel_input();
                }
                return;
            }
            if(!m_open || m_edit.waiting())
                return;
            if(m_error_only && operation != "cancel")
                return;
            if(operation == "cancel") {
                m_close_requested = true;
                m_runtime->cancel_input();
            } else if(operation == "apply") {
                if(m_has_actions) {
                    m_edit.apply();
                    m_runtime->cancel_input();
                }
            } else if(operation == "restore") {
                m_edit.restore_all();
                m_rows_dirty = true;
            } else if(operation == "previous" || operation == "next") {
                const auto count = m_edit.defaults().actions().size();
                if(count != 0) {
                    m_selected_action = operation == "next"
                                            ? (m_selected_action + 1) % count
                                            : (m_selected_action + count - 1) % count;
                    m_edit.cancel_capture();
                    m_rows_dirty = true;
                }
            } else if(arguments.size() == 3) {
                const auto action = Comet::Uuid::parse(arguments[1].Get<Rml::String>());
                const auto binding = Comet::Uuid::parse(arguments[2].Get<Rml::String>());
                if(!action || !binding)
                    return;
                if(operation == "key" || operation == "pad") {
                    m_view_error.clear();
                    m_pending_capture = PendingCapture{*action, *binding,
                        operation == "pad" ? Comet::PlayerInputEdit::CaptureKind::GamepadButton
                                           : Comet::PlayerInputEdit::CaptureKind::Keyboard};
                    m_runtime->stop_dispatch_preserve_focus();
                } else if(operation == "restore_binding") {
                    m_edit.restore_binding(*action, *binding);
                    m_rows_dirty = true;
                } else if(operation == "toggle_binding") {
                    m_edit.disable_binding(
                        *action, *binding, !m_edit.binding_patch(*action, *binding).disabled);
                    m_rows_dirty = true;
                }
            }
        }

        std::string button(const std::string& operation, const std::string& caption,
            const Comet::Uuid action, const Comet::Uuid binding) const {
            const auto binding_id = binding.to_string();
            return "<button id=\"" + operation + "-" + binding_id
                   + "\" data-attrif-disabled=\"waiting\" data-event-click=\"command('" + operation
                   + "','" + action.to_string() + "','" + binding_id + "')\">" + caption
                   + "</button>";
        }

        void sync_rows() {
            m_rows_dirty = false;
            const auto& actions = m_edit.defaults().actions();
            if(actions.empty()) {
                m_action_name = "暂无可配置动作";
                m_document->GetElementById("bindings")
                    ->SetInnerRML("<p class=\"muted\">项目没有玩家输入动作。</p>");
                return;
            }
            m_selected_action = std::min(m_selected_action, actions.size() - 1);
            const auto& action = actions[m_selected_action];
            m_action_name = action.name;
            std::string contents;
            const auto* action_patch = m_edit.action_patch(action.id);
            const bool action_disabled = action_patch && action_patch->disabled;
            const bool incompatible = action_patch && action_patch->type != action.type;
            if(action_disabled)
                contents += "<p class=\"muted\">此动作已禁用；恢复全部默认可重新启用。</p>";
            if(incompatible)
                contents +=
                    "<p class=\"muted\">覆盖配置与项目动作类型不一致；恢复全部默认后再编辑。</p>";
            for(const auto& binding : action.bindings) {
                const auto patch = m_edit.binding_patch(action.id, binding.id);
                auto effective = binding;
                if(patch.control)
                    effective.control = *patch.control;
                if(patch.scale)
                    effective.scale = *patch.scale;
                if(patch.deadzone)
                    effective.deadzone = *patch.deadzone;
                const auto formatted = Comet::InputActions::format_binding(effective);
                const auto label = formatted ? formatted.value().control : "配置不兼容";
                contents += "<div class=\"binding\"><span class=\"binding-name\">"
                            + escape_rml(label) + (patch.disabled ? " · 已禁用" : "") + "</span>";
                if(!action_disabled && !incompatible) {
                    if(std::holds_alternative<Comet::Input::Key>(effective.control))
                        contents += button("key", "录入按键", action.id, binding.id);
                    else if(std::holds_alternative<Comet::Input::GamepadButton>(effective.control))
                        contents += button("pad", "录入手柄按钮", action.id, binding.id);
                    else
                        contents += "<span class=\"binding-note\">此输入保留项目配置。</span>";
                    contents += button(
                        "toggle_binding", patch.disabled ? "启用" : "禁用", action.id, binding.id);
                }
                contents += button("restore_binding", "恢复此绑定", action.id, binding.id);
                contents += "</div>";
            }
            const auto* focused = m_context->GetFocusElement();
            const auto focus_id = focused ? focused->GetId() : std::string{};
            m_document->GetElementById("bindings")->SetInnerRML(contents);
            if(!focus_id.empty()) {
                if(auto* replacement = m_document->GetElementById(focus_id))
                    replacement->Focus(true);
            }
        }

        template<typename T> void set_variable(const char* name, T& destination, T value) {
            if(destination != value) {
                destination = std::move(value);
                m_model.DirtyVariable(name);
            }
        }

        void sync() {
            if(!m_document)
                return;
            if(m_rows_dirty) {
                sync_rows();
                m_model.DirtyVariable("action_name");
            }
            set_variable("waiting", m_waiting, m_edit.waiting());
            set_variable("has_actions", m_has_actions,
                !m_edit.defaults().actions().empty() && !m_error_only);
            set_variable(
                "error_text", m_error_text, !m_view_error.empty() ? m_view_error : m_edit.error());
            Rml::String status = "更改会在应用后保存。Esc 为菜单保留键。";
            if(m_waiting)
                status = "正在保存…";
            else if(m_edit.capture())
                status = "等待输入… Esc 取消录入。";
            if(m_edit.resolution() && !m_edit.resolution().value().issues.empty())
                status += " 部分旧覆盖配置已回退，未编辑的记录会保留。";
            set_variable("status_text", m_status_text, std::move(status));
            if(m_menu_displayed != m_open) {
                m_document->GetElementById("menu")->SetProperty(
                    "display", m_open ? "block" : "none");
                m_document->GetElementById("settings")
                    ->SetProperty("tab-index", m_open ? "none" : "auto");
                m_menu_displayed = m_open;
            }
        }

        bool is_menu_control(Rml::Element* element) const {
            if(!element || element->GetOwnerDocument() != m_document || !element->IsVisible(true)
                || element->HasAttribute("disabled")
                || element->GetComputedValues().tab_index() != Rml::Style::TabIndex::Auto
                || element->GetComputedValues().focus() == Rml::Style::Focus::None)
                return false;
            const auto* menu = m_document->GetElementById("menu");
            for(auto* parent = element; parent; parent = parent->GetParentNode()) {
                if(parent == menu)
                    return true;
            }
            return false;
        }

        void focus_first_binding() {
            Rml::ElementList controls;
            m_document->GetElementById("bindings")->QuerySelectorAll(controls, "button");
            for(auto* element : controls) {
                if(is_menu_control(element) && element->Focus(true))
                    return;
            }
            auto* cancel = m_document->GetElementById("cancel");
            if(is_menu_control(cancel))
                cancel->Focus(true);
        }

        void close() {
            if(m_edit.waiting()) {
                m_close_requested = false;
                return;
            }
            m_edit.clear();
            m_open = false;
            m_error_only = false;
            m_close_requested = false;
            m_open_requested = false;
            m_pending_capture.reset();
            m_capture_focus.clear();
            m_runtime->set_capture_active(false);
            m_runtime->cancel_input();
            sync();
        }

        struct PendingCapture {
            Comet::Uuid action;
            Comet::Uuid binding;
            Comet::PlayerInputEdit::CaptureKind kind;
        };
        Comet::Window& m_window;
        std::unique_ptr<Comet::Ui::RmlContext> m_runtime;
        Rml::Context* m_context;
        Rml::ElementDocument* m_document = nullptr;
        Rml::DataModelHandle m_model;
        Comet::PlayerInputEdit m_edit;
        Rml::String m_fps_text;
        Rml::String m_action_name;
        Rml::String m_status_text;
        Rml::String m_error_text;
        std::string m_view_error;
        std::string m_capture_focus;
        std::optional<PendingCapture> m_pending_capture;
        std::size_t m_selected_action = 0;
        bool m_menu_available = true;
        bool m_waiting = false;
        bool m_has_actions = false;
        bool m_open = false;
        bool m_error_only = false;
        bool m_rows_dirty = true;
        bool m_open_requested = false;
        bool m_close_requested = false;
        bool m_pointer_blocked = false;
        bool m_menu_displayed = false;
        int m_displayed_fps = -1;
    };

    PlayerInputMenu::PlayerInputMenu(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    PlayerInputMenu::~PlayerInputMenu() = default;

    Comet::Result<std::unique_ptr<PlayerInputMenu>, Comet::Error> PlayerInputMenu::create(
        Comet::Window& window, Comet::Renderer& renderer, Options options) {
        using Creation = Comet::Result<std::unique_ptr<PlayerInputMenu>, Comet::Error>;
        if(options.resource_root.empty())
            options.resource_root = COMET_SAMPLE_UI_RESOURCE_ROOT;
        auto backend = Comet::Ui::RmlContext::create(window, renderer, std::move(options));
        if(!backend)
            return Creation::failure(backend.error());
        auto impl = std::make_unique<Impl>(window, std::move(backend).value());
        if(auto initialized = impl->initialize(); !initialized)
            return Creation::failure({initialized.error()});
        return Creation::success(
            std::unique_ptr<PlayerInputMenu>(new PlayerInputMenu(std::move(impl))));
    }

    bool PlayerInputMenu::frame(const Comet::Input::Frame& input, const HudStats& hud) {
        auto& ui = *m_impl;
        const bool was_open = ui.m_open;
        const bool was_capturing = ui.m_edit.capture().has_value();
        const auto fps = std::isfinite(hud.fps)
                             ? static_cast<int>(std::round(std::clamp(hud.fps, 0.0f, 100000.0f)))
                             : 0;
        if(fps != ui.m_displayed_fps) {
            ui.m_displayed_fps = fps;
            ui.m_fps_text = std::to_string(std::max(fps, 0)) + " FPS";
            ui.m_model.DirtyVariable("fps_text");
        }
        if(ui.m_menu_available != hud.menu_available) {
            ui.m_menu_available = hud.menu_available;
            ui.m_model.DirtyVariable("menu_available");
        }
        if(input.focused && !ui.m_open && hud.menu_available
            && input.key(Comet::Input::Key::F1).pressed)
            ui.m_open_requested = true;
        const auto gamepad = input.first_connected_gamepad();
        if(input.focused && !ui.m_open && hud.menu_available && gamepad
            && input.gamepads[*gamepad].button(Comet::Input::GamepadButton::Start).pressed)
            ui.m_open_requested = true;
        if(input.focused && !was_capturing && input.key(Comet::Input::Key::F6).pressed) {
            const auto reloaded = ui.reload();
            if(!reloaded)
                LOG_WARN("Runtime UI reload failed: {}", reloaded.error());
        }
        ui.m_runtime->set_capture_active(was_capturing);
        if(ui.m_open && !ui.m_edit.waiting() && !was_capturing && input.focused
            && input.key(Comet::Input::Key::Escape).pressed)
            ui.m_close_requested = true;
        if(ui.m_open && !ui.m_edit.waiting() && !was_capturing && input.focused && gamepad
            && input.gamepads[*gamepad].button(Comet::Input::GamepadButton::East).pressed)
            ui.m_close_requested = true;
        ui.m_runtime->process_input(input, ui.m_open && !ui.m_close_requested);
        if(was_capturing) {
            const auto* focused = ui.m_context->GetFocusElement();
            const bool owns_focus = focused && focused->GetOwnerDocument() == ui.m_document
                                    && focused->GetId() == ui.m_capture_focus;
            const auto& capture = ui.m_edit.capture();
            if(capture && capture->gamepad && owns_focus && input.focused
                && input.serial > capture->serial
                && input.gamepads[*capture->gamepad]
                    .button(Comet::Input::GamepadButton::Start)
                    .pressed) {
                ui.m_edit.cancel_capture();
                ui.m_view_error = "Start 是设置菜单保留按钮，请使用其他按钮。";
            } else {
                ui.m_edit.capture_input(
                    input, ui.m_open && owns_focus && !ui.m_runtime->text_input_active());
            }
            if(!ui.m_edit.capture())
                ui.m_rows_dirty = true;
        }
        if(ui.m_pending_capture) {
            const auto capture = *ui.m_pending_capture;
            ui.m_pending_capture.reset();
            const auto* focused = ui.m_context->GetFocusElement();
            if(input.focused && focused && focused->GetOwnerDocument() == ui.m_document
                && !ui.m_runtime->text_input_active()) {
                ui.m_capture_focus = focused->GetId();
                ui.m_edit.start_capture(capture.action, capture.binding, input, capture.kind);
                ui.m_runtime->set_capture_active(ui.m_edit.capture().has_value());
            }
        }
        if(ui.m_close_requested)
            ui.close();
        ui.sync();
        if(!ui.m_context->Update()) {
            ui.m_view_error = "运行时界面更新失败。";
            ui.sync();
        }
        if(ui.m_open && !ui.m_close_requested && input.focused && !ui.m_edit.waiting()
            && !ui.m_edit.capture() && !ui.is_menu_control(ui.m_context->GetFocusElement()))
            ui.focus_first_binding();
        ui.m_pointer_blocked = ui.m_runtime->pointer_blocked();
        return was_open || ui.m_open || ui.m_open_requested;
    }

    bool PlayerInputMenu::is_open() const {
        return m_impl->m_open;
    }
    bool PlayerInputMenu::pointer_blocked() const {
        return m_impl->m_pointer_blocked;
    }
    bool PlayerInputMenu::take_open_request() {
        return std::exchange(m_impl->m_open_requested, false);
    }

    void PlayerInputMenu::open(const Comet::InputActions& defaults,
        const Comet::InputOverrides& current,
        const std::span<const Comet::Input::Key> reserved_keys) {
        auto& ui = *m_impl;
        if(ui.m_edit.waiting())
            return;
        ui.m_edit.reset(defaults, current, reserved_keys);
        ui.m_open = true;
        ui.m_error_only = false;
        ui.m_view_error.clear();
        ui.m_selected_action = 0;
        ui.m_rows_dirty = true;
        ui.m_window.set_cursor_locked(false);
        ui.m_runtime->cancel_input();
        ui.sync();
        ui.m_context->Update();
        ui.focus_first_binding();
    }

    void PlayerInputMenu::close() {
        m_impl->close();
    }
    void PlayerInputMenu::show_error(std::string error) {
        auto& ui = *m_impl;
        ui.m_view_error = std::move(error);
        ui.m_open = true;
        ui.m_error_only = true;
        ui.m_window.set_cursor_locked(false);
        ui.sync();
        ui.m_context->Update();
        ui.m_document->GetElementById("cancel")->Focus(true);
    }
    std::optional<Comet::InputOverrides> PlayerInputMenu::take_request() {
        return m_impl->m_edit.take_request();
    }
    void PlayerInputMenu::complete(const Comet::Result<void>& result) {
        auto& ui = *m_impl;
        if(ui.m_edit.complete(result))
            ui.close();
        else
            ui.sync();
    }
    Comet::Result<void> PlayerInputMenu::reload_documents() {
        return m_impl->reload();
    }
    Comet::Result<void, Comet::GraphicsError> PlayerInputMenu::render(
        Comet::OverlayRecordContext& context) {
        return m_impl->m_runtime->render(context);
    }
    void PlayerInputMenu::release_swapchain_resources() {
        m_impl->m_runtime->release_swapchain_resources();
    }
    Comet::Result<void, Comet::GraphicsError> PlayerInputMenu::rebuild_swapchain_resources(
        const Comet::SwapchainCompatibility& compatibility) {
        return m_impl->m_runtime->rebuild_swapchain_resources(compatibility);
    }
}
