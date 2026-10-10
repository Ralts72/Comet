#include "ui/project_ui.h"

#include "ui/lua_controller.h"
#include "common/scope_exit.h"
#include "core/window.h"
#include "render/renderer.h"
#include "diagnostics/logger.h"
#include "input/player_input_edit.h"

#include <RmlUi/Core.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <utility>
#include <variant>
#include "core/frame_pacer.h"

namespace Comet::Ui {
    namespace {
        constexpr std::size_t max_model_fields = 128;
        constexpr std::size_t max_text_bytes = 256 * 1024;
        constexpr const char* model_name = "ui";

        Result<std::filesystem::path> resolve_entry(
            const std::filesystem::path& root, const std::filesystem::path& entry) {
            std::error_code error;
            const auto path = std::filesystem::canonical(root / entry, error);
            const auto relative = path.lexically_relative(root);
            if(error || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                return Result<std::filesystem::path>::failure(
                    "UI source is missing or outside project assets: " + entry.generic_string());
            return Result<std::filesystem::path>::success(path);
        }
        bool eligible(const Rml::Element* element, const Rml::Element* root) {
            if(!element || !element->IsVisible(true) || element->HasAttribute("disabled")
                || element->GetComputedValues().tab_index() != Rml::Style::TabIndex::Auto)
                return false;
            for(auto* parent = element; parent; parent = parent->GetParentNode()) {
                if(parent == root)
                    return true;
            }
            return false;
        }
    }

    class ProjectUi::Impl final {
    public:
        enum class Api {
            Set,
            RequireElement,
            Property,
            Markup,
            Modal,
            Focus,
            FocusFirst,
            HasFocus,
            Pressed,
            StopInput,
            Reload,
            InputBegin,
            InputEnd,
            InputStatus,
            InputActions,
            InputRestore,
            InputRestoreBinding,
            InputToggleBinding,
            InputCapture,
            InputApply,
            DisplaySettings,
            DisplayApply,
            DisplayConfirm,
            DisplayRevert,
            QualitySettings,
            QualityApply,
            AudioSettings,
            AudioApply
        };
        class Controller final {
        public:
            explicit Controller(Impl& host)
                : m_host(host),
                  m_lua({{"set", static_cast<int>(Api::Set)},
                            {"require_element", static_cast<int>(Api::RequireElement)},
                            {"property", static_cast<int>(Api::Property)},
                            {"markup", static_cast<int>(Api::Markup)},
                            {"modal", static_cast<int>(Api::Modal)},
                            {"focus", static_cast<int>(Api::Focus)},
                            {"focus_first", static_cast<int>(Api::FocusFirst)},
                            {"has_focus", static_cast<int>(Api::HasFocus)},
                            {"pressed", static_cast<int>(Api::Pressed)},
                            {"stop_input", static_cast<int>(Api::StopInput)},
                            {"reload", static_cast<int>(Api::Reload)},
                            {"input_begin", static_cast<int>(Api::InputBegin)},
                            {"input_end", static_cast<int>(Api::InputEnd)},
                            {"input_status", static_cast<int>(Api::InputStatus)},
                            {"input_actions", static_cast<int>(Api::InputActions)},
                            {"input_restore", static_cast<int>(Api::InputRestore)},
                            {"input_restore_binding", static_cast<int>(Api::InputRestoreBinding)},
                            {"input_toggle_binding", static_cast<int>(Api::InputToggleBinding)},
                            {"input_capture", static_cast<int>(Api::InputCapture)},
                            {"input_apply", static_cast<int>(Api::InputApply)},
                            {"display_settings", static_cast<int>(Api::DisplaySettings)},
                            {"display_apply", static_cast<int>(Api::DisplayApply)},
                            {"display_confirm", static_cast<int>(Api::DisplayConfirm)},
                            {"display_revert", static_cast<int>(Api::DisplayRevert)},
                            {"quality_settings", static_cast<int>(Api::QualitySettings)},
                            {"quality_apply", static_cast<int>(Api::QualityApply)},
                            {"audio_settings", static_cast<int>(Api::AudioSettings)},
                            {"audio_apply", static_cast<int>(Api::AudioApply)}},
                      [this](lua_State* state, int api) {
                          return m_host.api(*this, state, static_cast<Api>(api));
                      }) {}
            Result<void> load(const std::filesystem::path& path) { return m_lua.load(path); }
            Result<void> copy_state(const Controller& previous) {
                m_modal = previous.m_modal;
                return m_lua.copy_state(previous.m_lua);
            }
            Result<void> call(const char* method, const Rml::VariantList& arguments = {}) {
                if(std::string_view(method) == "on_frame")
                    return m_lua.frame({m_host.m_info.fps, m_host.m_info.game_available,
                        m_host.m_input && m_host.m_input->focused, m_host.m_info.vsync_active});
                return m_lua.call(method, arguments);
            }
            Impl& m_host;
            Detail::LuaController m_lua;
            Rml::ElementDocument* m_document = nullptr;
            std::string m_focus;
            bool m_focus_first = false;
            bool m_modal = false;
            bool m_preparing = true;
            bool m_retired = false;
        };

        Impl(Window& window, Renderer& renderer, std::unique_ptr<RmlContext> runtime,
            Project::UiEntry entry, std::filesystem::path root, Services services)
            : m_window(window), m_renderer(renderer),
              m_supported_msaa(renderer.supported_msaa_samples()), m_runtime(std::move(runtime)),
              m_entry(std::move(entry)), m_resource_root(std::move(root)),
              m_services(std::move(services)) {}
        ~Impl() {
            retire();
            m_controller.reset();
            m_runtime.reset();
        }
        Result<void> initialize() {
            auto constructor = m_runtime->context().CreateDataModel(model_name);
            if(!constructor
                || !constructor.BindEventCallback(
                    "command", [this](Rml::DataModelHandle, Rml::Event& event,
                                   const Rml::VariantList& arguments) {
                        const auto* target = event.GetTargetElement();
                        if(m_controller && target && !m_runtime->is_loading_document() && !m_calling
                            && target->GetOwnerDocument() == m_controller->m_document) {
                            if(m_dispatching) {
                                if(auto result = call("on_event", arguments); !result)
                                    m_event_error = result.error();
                            } else if(m_events.size() < 64)
                                m_events.push_back(arguments);
                        }
                    }))
                return Result<void>::failure("Cannot create project UI data model");
            m_model = constructor.GetModelHandle();
            return reload();
        }
        Result<void> call(const char* method, const Rml::VariantList& arguments = {}) {
            const ScopeExit finish([this] { m_calling = false; });
            m_calling = true;
            return m_controller->call(method, arguments);
        }
        void retire() {
            if(!m_controller)
                return;
            m_controller->m_retired = true;
            m_controller->m_document = nullptr;
            if(auto result = call("on_destroy"); !result)
                LOG_WARN("UI controller cleanup failed: {}", result.error());
        }
        Result<void> reload() {
            const auto document = resolve_entry(m_resource_root, m_entry.document);
            if(!document)
                return Result<void>::failure(document.error());
            const auto path = resolve_entry(m_resource_root, m_entry.controller);
            if(!path)
                return Result<void>::failure(path.error());
            auto candidate = std::make_unique<Controller>(*this);
            if(auto loaded = candidate->load(path.value()); !loaded)
                return loaded;
            if(m_controller) {
                if(auto copied = candidate->copy_state(*m_controller); !copied)
                    return copied;
            }
            auto constructor = m_runtime->context().GetDataModel(model_name);
            for(const auto& [name, value] : candidate->m_lua.model()) {
                if(m_fields.contains(name))
                    continue;
                if(m_fields.size() >= max_model_fields)
                    return Result<void>::failure("UI data model exceeds 128 fields across reloads");
                if(!constructor.BindFunc(name, [this, name](Rml::Variant& result) {
                       if(m_presented) {
                           if(const auto value = m_presented->m_lua.model().find(name);
                               value != m_presented->m_lua.model().end())
                               result = value->second;
                       }
                   }))
                    return Result<void>::failure("Cannot bind UI model field: " + name);
                m_fields.insert(name);
            }
            m_presented = candidate.get();
            m_model.DirtyAllVariables();
            const ScopeExit restore([this] {
                m_presented = m_controller.get();
                m_model.DirtyAllVariables();
                (void)m_runtime->update();
            });
            auto replacement = m_runtime->replace_document(
                m_controller ? m_controller->m_document : nullptr, m_entry.document,
                [&](Rml::ElementDocument& document) {
                    candidate->m_document = &document;
                    return candidate->call("on_mount", {Rml::Variant(bool(m_controller))});
                },
                [&](Rml::ElementDocument&) { return candidate->call("on_present"); });
            if(!replacement)
                return Result<void>::failure(replacement.error());
            retire();
            m_controller = std::move(candidate);
            m_controller->m_document = replacement.value();
            m_controller->m_preparing = false;
            m_edit.cancel_capture();
            m_pending_capture.reset();
            m_events.clear();
            m_pending_markup.clear();
            m_focus_requested = false;
            m_runtime->cancel_input();
            m_runtime->set_capture_active(false);
            if(m_controller->m_modal)
                m_window.set_cursor_locked(false);
            focus(*m_controller);
            return Result<void>::success();
        }

        void focus(Controller& vm) {
            auto* root = vm.m_document->GetElementById(vm.m_focus);
            if(!root)
                return;
            if(!vm.m_focus_first) {
                root->Focus(true);
                return;
            }
            Rml::ElementList elements;
            root->QuerySelectorAll(elements, "button, input, select, textarea");
            for(auto* element : elements) {
                if(eligible(element, root)) {
                    element->Focus(true);
                    return;
                }
            }
        }
        bool replace_markup(Controller& vm, const std::string& id, const std::string& contents) {
            auto* target = vm.m_document->GetElementById(id);
            if(!target)
                return false;
            const auto* focused = m_runtime->context().GetFocusElement();
            const auto focus_id = focused ? focused->GetId() : std::string{};
            target->SetInnerRML(contents);
            if(!vm.m_preparing && !focus_id.empty()) {
                if(auto* replacement = vm.m_document->GetElementById(focus_id))
                    replacement->Focus(true);
            }
            return true;
        }
        int api(Controller& vm, lua_State* state, Api operation);
        int input_api(lua_State* state, Api operation);
        int display_api(lua_State* state, Api operation);
        int quality_api(lua_State* state, Api operation);
        int audio_api(lua_State* state, Api operation);
        Result<FrameResult, Error> frame(const Input::Frame& input, FrameInfo info);
        void deactivate() {
            if(m_controller) {
                if(auto result = call("on_deactivate"); !result)
                    LOG_WARN("UI controller deactivate failed: {}", result.error());
                m_controller->m_modal = false;
            }
            m_edit.clear();
            m_edit_active = false;
            ++m_edit_revision;
            m_pending_capture.reset();
            m_events.clear();
            m_pending_display.reset();
            m_pending_quality.reset();
            m_pending_audio.reset();
            m_runtime->set_capture_active(false);
            m_runtime->cancel_input();
            (void)m_runtime->update();
        }

        struct PendingCapture {
            Uuid action;
            Uuid binding;
            PlayerInputEdit::CaptureKind kind;
        };
        struct BindingView {
            std::string id, source, control;
            bool disabled = false;
        };
        struct ActionView {
            std::string id, name;
            bool disabled = false, compatible = true;
            std::vector<BindingView> bindings;
        };
        void snapshot_actions() {
            m_actions.clear();
            for(const auto& action : m_edit.defaults().actions()) {
                const auto* patch = m_edit.action_patch(action.id);
                ActionView view{action.id.to_string(), action.name, patch && patch->disabled,
                    !patch || patch->type == action.type, {}};
                for(auto binding : action.bindings) {
                    const auto override = m_edit.binding_patch(action.id, binding.id);
                    if(override.control)
                        binding.control = *override.control;
                    if(override.scale)
                        binding.scale = *override.scale;
                    if(override.deadzone)
                        binding.deadzone = *override.deadzone;
                    const auto formatted = InputActions::format_binding(binding);
                    BindingView row;
                    row.id = binding.id.to_string();
                    row.disabled = override.disabled;
                    if(formatted) {
                        row.source = formatted.value().source;
                        row.control = formatted.value().control;
                    }
                    view.bindings.push_back(std::move(row));
                }
                m_actions.push_back(std::move(view));
            }
        }

        Window& m_window;
        Renderer& m_renderer;
        const std::vector<uint32_t> m_supported_msaa;
        std::unique_ptr<RmlContext> m_runtime;
        Project::UiEntry m_entry;
        std::filesystem::path m_resource_root;
        Services m_services;
        std::unique_ptr<Controller> m_controller;
        Controller* m_presented = nullptr;
        Rml::DataModelHandle m_model;
        std::set<std::string> m_fields;
        std::vector<Rml::VariantList> m_events;
        PlayerInputEdit m_edit;
        std::vector<ActionView> m_actions;
        std::vector<Input::GamepadButton> m_reserved_buttons;
        std::optional<PendingCapture> m_pending_capture;
        enum class DisplayDecision { Confirm, Revert };
        std::optional<std::variant<Comet::DisplaySettings, DisplayDecision>> m_pending_display;
        std::optional<Comet::QualitySettings> m_pending_quality;
        std::optional<Comet::AudioSettings> m_pending_audio;
        std::string m_capture_focus, m_api_error, m_event_error;
        std::map<std::string, std::string> m_pending_markup;
        const Input::Frame* m_input = nullptr;
        FrameInfo m_info;
        std::uint64_t m_edit_revision = 0;
        bool m_edit_active = false, m_calling = false, m_dispatching = false;
        bool m_focus_requested = false;
        bool m_blocked = false, m_reload_requested = false;
    };

    int ProjectUi::Impl::api(Controller& vm, lua_State* state, Api operation) {
        if(vm.m_retired)
            return luaL_error(state, "UI controller has been retired");
        const bool presentation = operation <= Api::HasFocus || operation == Api::InputStatus
                                  || operation == Api::InputActions
                                  || operation == Api::DisplaySettings
                                  || operation == Api::QualitySettings;
        if(vm.m_preparing && !presentation)
            return luaL_error(state, "UI services are unavailable before controller publication");
        if(operation >= Api::InputRestore && operation <= Api::InputApply && !m_edit_active)
            return luaL_error(state, "Player input transaction has not been opened");
        auto element = [&](const char* id) { return vm.m_document->GetElementById(id); };
        switch(operation) {
            case Api::Set: {
                const auto* name = luaL_checkstring(state, 1);
                bool valid = false;
                {
                    const auto found = vm.m_lua.model().find(name);
                    Rml::Variant value;
                    valid = found != vm.m_lua.model().end()
                            && Detail::LuaController::scalar(state, 2, value);
                    if(valid && found->second != value) {
                        found->second = std::move(value);
                        m_model.DirtyVariable(name);
                    }
                }
                if(!valid)
                    return luaL_error(state, "Unknown model field or unsupported scalar: %s", name);
                return 0;
            }
            case Api::RequireElement: {
                const auto* id = luaL_checkstring(state, 1);
                if(!element(id))
                    return luaL_error(state, "UI document is missing element: %s", id);
                return 0;
            }
            case Api::Property: {
                const auto* id = luaL_checkstring(state, 1);
                const auto* name = luaL_checkstring(state, 2);
                const auto* value = luaL_checkstring(state, 3);
                auto* target = element(id);
                if(!target || !target->SetProperty(name, value))
                    return luaL_error(state, "Cannot set UI property for element: %s", id);
                return 0;
            }
            case Api::Markup: {
                const auto* id = luaL_checkstring(state, 1);
                std::size_t size = 0;
                const auto* markup = luaL_checklstring(state, 2, &size);
                auto* target = element(id);
                if(!target || size > max_text_bytes)
                    return luaL_error(state, "Invalid UI markup target or size: %s", id);
                {
                    if(!vm.m_preparing)
                        m_pending_markup[id] = std::string(markup, size);
                    else
                        (void)replace_markup(vm, id, std::string(markup, size));
                }
                return 0;
            }
            case Api::Modal: {
                const bool active = lua_toboolean(state, 1);
                if(vm.m_modal != active && !vm.m_preparing) {
                    m_blocked |= vm.m_modal || active;
                    m_runtime->cancel_input();
                    if(active)
                        m_window.set_cursor_locked(false);
                }
                vm.m_modal = active;
                return 0;
            }
            case Api::Focus:
            case Api::FocusFirst: {
                const auto* id = luaL_checkstring(state, 1);
                if(!element(id))
                    return luaL_error(state, "Cannot focus missing UI element: %s", id);
                vm.m_focus = id;
                vm.m_focus_first = operation == Api::FocusFirst;
                if(!vm.m_preparing)
                    m_focus_requested = true;
                return 0;
            }
            case Api::HasFocus: {
                const auto* id = luaL_checkstring(state, 1);
                lua_pushboolean(
                    state, eligible(m_runtime->context().GetFocusElement(), element(id)));
                return 1;
            }
            case Api::Pressed: {
                const auto* source = luaL_checkstring(state, 1);
                const auto* control = luaL_checkstring(state, 2);
                bool pressed = false;
                {
                    const auto binding = InputActions::parse_binding(source, control);
                    if(binding && m_input && m_input->focused) {
                        if(const auto* key = std::get_if<Input::Key>(&binding.value().control))
                            pressed = m_input->key(*key).pressed;
                        if(const auto* button =
                                std::get_if<Input::GamepadButton>(&binding.value().control)) {
                            const auto pad = m_input->first_connected_gamepad();
                            pressed = pad && m_input->gamepads[*pad].button(*button).pressed;
                        }
                    }
                }
                lua_pushboolean(state, pressed);
                return 1;
            }
            case Api::StopInput:
                m_runtime->stop_dispatch_preserve_focus();
                return 0;
            case Api::Reload:
                if(!m_edit.capture())
                    m_reload_requested = true;
                return 0;
            case Api::DisplaySettings:
            case Api::DisplayApply:
            case Api::DisplayConfirm:
            case Api::DisplayRevert:
                return display_api(state, operation);
            case Api::QualitySettings:
            case Api::QualityApply:
                return quality_api(state, operation);
            case Api::AudioSettings:
            case Api::AudioApply:
                return audio_api(state, operation);
            default:
                return input_api(state, operation);
        }
    }

    int ProjectUi::Impl::display_api(lua_State* state, Api operation) {
        if(operation == Api::DisplaySettings) {
            if(!m_services.load_display) {
                lua_pushnil(state);
                return 1;
            }
            auto loaded = m_services.load_display();
            if(!loaded) {
                lua_pushnil(state);
                lua_pushlstring(state, loaded.error().data(), loaded.error().size());
                return 2;
            }
            const auto push_settings = [&](const Comet::DisplaySettings& settings) {
                lua_createtable(state, 0, 8);
                lua_pushinteger(state, settings.width);
                lua_setfield(state, -2, "width");
                lua_pushinteger(state, settings.height);
                lua_setfield(state, -2, "height");
                const auto mode = Comet::DisplaySettings::mode_name(settings.mode);
                lua_pushlstring(state, mode.data(), mode.size());
                lua_setfield(state, -2, "mode");
                lua_pushboolean(state, settings.vsync);
                lua_setfield(state, -2, "vsync");
                lua_pushinteger(state, settings.frame_rate_limit);
                lua_setfield(state, -2, "frame_rate_limit");
                const auto output_mode = OutputSettings::mode_name(settings.output.mode);
                lua_pushlstring(state, output_mode.data(), output_mode.size());
                lua_setfield(state, -2, "output_mode");
                lua_pushnumber(state, settings.output.hdr_headroom);
                lua_setfield(state, -2, "hdr_headroom");
                lua_pushnumber(state, settings.output.hdr_white_level);
                lua_setfield(state, -2, "hdr_white_level");
            };
            push_settings(loaded.value());
            lua_pushboolean(state, m_info.display_preview);
            lua_setfield(state, -2, "preview");
            lua_pushboolean(state, m_renderer.is_hdr_output());
            lua_setfield(state, -2, "hdr_active");
            lua_pushboolean(state, m_renderer.output_pending());
            lua_setfield(state, -2, "output_pending");
            const auto confirmation = m_services.display_confirmation
                                          ? m_services.display_confirmation()
                                          : std::optional<float>{};
            lua_pushboolean(state, confirmation.has_value());
            lua_setfield(state, -2, "confirmation_pending");
            lua_pushnumber(state, confirmation.value_or(0));
            lua_setfield(state, -2, "confirmation_seconds");
            push_settings(m_services.display_defaults);
            lua_setfield(state, -2, "defaults");
            return 1;
        }
        if(operation == Api::DisplayConfirm || operation == Api::DisplayRevert) {
            const auto& service = operation == Api::DisplayConfirm ? m_services.confirm_display
                                                                   : m_services.revert_display;
            if(!service || !m_info.game_available)
                return luaL_error(state, "Display confirmation service is unavailable");
            m_pending_display = operation == Api::DisplayConfirm ? DisplayDecision::Confirm
                                                                 : DisplayDecision::Revert;
            return 0;
        }
        const auto width = luaL_checkinteger(state, 1);
        const auto height = luaL_checkinteger(state, 2);
        const auto* mode = luaL_checkstring(state, 3);
        luaL_checktype(state, 4, LUA_TBOOLEAN);
        WindowMode window_mode = WindowMode::Windowed;
        bool valid_mode = false;
        {
            const auto parsed = Comet::DisplaySettings::parse_mode(mode);
            valid_mode = bool(parsed);
            if(parsed)
                window_mode = parsed.value();
        }
        if(width <= 0 || height <= 0 || width > std::numeric_limits<int>::max()
            || height > std::numeric_limits<int>::max() || !valid_mode)
            return luaL_error(state, "Invalid display settings");
        if(!m_services.apply_display || !m_info.game_available)
            return luaL_error(state, "Display settings service is unavailable");
        OutputSettings output;
        int frame_rate_limit = 0;
        const bool keep_output = lua_isnoneornil(state, 5);
        const bool keep_limit = lua_isnoneornil(state, 8);
        if(keep_output || keep_limit) {
            bool loaded = false;
            if(m_services.load_display) {
                const auto settings = m_services.load_display();
                if(settings) {
                    output = settings.value().output;
                    frame_rate_limit = settings.value().frame_rate_limit;
                    loaded = true;
                }
            }
            if(!loaded)
                return luaL_error(state, "Display settings are unavailable");
        }
        if(!keep_output) {
            const auto* output_mode = luaL_checkstring(state, 5);
            const auto headroom = luaL_checknumber(state, 6);
            const auto white = luaL_checknumber(state, 7);
            bool valid_output = false;
            {
                const auto parsed = OutputSettings::parse_mode(output_mode);
                if(parsed) {
                    output.mode = parsed.value();
                    valid_output = true;
                }
            }
            if(!valid_output || !std::isfinite(headroom) || headroom < 1 || headroom > 16
                || !std::isfinite(white) || white < 0.5 || white > 2)
                return luaL_error(state, "Invalid display output settings");
            output.hdr_headroom = static_cast<float>(headroom);
            output.hdr_white_level = static_cast<float>(white);
        }
        if(!keep_limit) {
            const auto limit = luaL_checkinteger(state, 8);
            if(limit < 0 || limit > FramePacer::MAX_LIMIT)
                return luaL_error(state, "Frame rate limit must be an integer from 0 to 1000");
            frame_rate_limit = static_cast<int>(limit);
        }
        m_pending_display =
            Comet::DisplaySettings{static_cast<int>(width), static_cast<int>(height), window_mode,
                bool(lua_toboolean(state, 4)), output, frame_rate_limit};
        return 0;
    }

    int ProjectUi::Impl::quality_api(lua_State* state, Api operation) {
        if(operation == Api::QualitySettings) {
            if(!m_services.load_quality) {
                lua_pushnil(state);
                return 1;
            }
            const auto loaded = m_services.load_quality();
            if(!loaded) {
                lua_pushnil(state);
                lua_pushlstring(state, loaded.error().data(), loaded.error().size());
                return 2;
            }
            const auto push_settings = [&](const Comet::QualitySettings& settings) {
                lua_createtable(state, 0, 3);
                lua_pushinteger(state, settings.msaa_samples);
                lua_setfield(state, -2, "msaa_samples");
                lua_pushnumber(state, settings.max_anisotropy);
                lua_setfield(state, -2, "max_anisotropy");
                lua_pushnumber(state, settings.render_scale);
                lua_setfield(state, -2, "render_scale");
            };
            push_settings(loaded.value());
            push_settings(m_services.quality_defaults);
            lua_setfield(state, -2, "defaults");
            push_settings(m_renderer.get_quality_settings());
            lua_setfield(state, -2, "active");
            const auto& samples = m_supported_msaa;
            lua_createtable(state, static_cast<int>(samples.size()), 0);
            for(std::size_t i = 0; i < samples.size(); ++i) {
                lua_pushinteger(state, samples[i]);
                lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(state, -2, "supported_msaa");
            lua_pushnumber(state, m_renderer.max_anisotropy());
            lua_setfield(state, -2, "max_anisotropy_supported");
            lua_pushboolean(state, m_renderer.quality_pending());
            lua_setfield(state, -2, "pending");
            const auto& error = m_renderer.quality_error();
            lua_pushlstring(state, error.data(), error.size());
            lua_setfield(state, -2, "error");
            return 1;
        }
        const auto samples = luaL_checkinteger(state, 1);
        const auto anisotropy = luaL_checknumber(state, 2);
        const auto scale = luaL_checknumber(state, 3);
        if(samples < 1 || samples > 8)
            return luaL_error(state, "Invalid MSAA sample count");
        if(!m_services.apply_quality || !m_info.game_available)
            return luaL_error(state, "Quality settings service is unavailable");
        m_pending_quality = Comet::QualitySettings{static_cast<uint32_t>(samples),
            static_cast<float>(anisotropy), static_cast<float>(scale)};
        return 0;
    }

    int ProjectUi::Impl::audio_api(lua_State* state, Api operation) {
        if(operation == Api::AudioSettings) {
            if(!m_services.load_audio || !m_services.active_audio) {
                lua_pushnil(state);
                return 1;
            }
            auto loaded = m_services.load_audio();
            if(!loaded) {
                lua_pushnil(state);
                lua_pushlstring(state, loaded.error().data(), loaded.error().size());
                return 2;
            }
            const auto push_settings = [&](const Comet::AudioSettings& settings) {
                lua_createtable(state, 0, 3);
                lua_pushnumber(state, settings.master_volume);
                lua_setfield(state, -2, "master_volume");
                lua_pushnumber(state, settings.effects_volume);
                lua_setfield(state, -2, "effects_volume");
                lua_pushnumber(state, settings.music_volume);
                lua_setfield(state, -2, "music_volume");
            };
            push_settings(loaded.value());
            push_settings(m_services.audio_defaults);
            lua_setfield(state, -2, "defaults");
            push_settings(m_services.active_audio());
            lua_setfield(state, -2, "active");
            return 1;
        }
        const auto master = luaL_checknumber(state, 1);
        const auto effects = luaL_checknumber(state, 2);
        const auto music = luaL_checknumber(state, 3);
        for(const auto volume : {master, effects, music}) {
            if(!std::isfinite(volume) || volume < 0 || volume > 1)
                return luaL_error(state, "Audio volumes must be finite values in [0, 1]");
        }
        if(!m_services.apply_audio || !m_info.game_available)
            return luaL_error(state, "Audio settings service is unavailable");
        m_pending_audio = Comet::AudioSettings{
            static_cast<float>(master), static_cast<float>(effects), static_cast<float>(music)};
        return 0;
    }

    int ProjectUi::Impl::input_api(lua_State* state, Api operation) {
        switch(operation) {
            case Api::InputBegin: {
                luaL_checktype(state, 1, LUA_TTABLE);
                luaL_checktype(state, 2, LUA_TTABLE);
                m_api_error.clear();
                if(!m_services.load_input || !m_info.game_available || m_edit.waiting()) {
                    m_api_error = "Player input settings are unavailable";
                } else {
                    std::vector<Input::Key> keys;
                    std::vector<Input::GamepadButton> buttons;
                    for(int table = 1; table <= 2; ++table) {
                        const auto count = lua_rawlen(state, table);
                        if(count > 32) {
                            m_api_error = "Too many reserved controls";
                            break;
                        }
                        for(std::size_t index = 1; index <= count; ++index) {
                            lua_rawgeti(state, table, static_cast<lua_Integer>(index));
                            if(lua_type(state, -1) != LUA_TSTRING) {
                                m_api_error = "Reserved controls must be strings";
                                lua_pop(state, 1);
                                break;
                            }
                            auto binding = InputActions::parse_binding(
                                table == 1 ? "key" : "gamepad_button", lua_tostring(state, -1));
                            lua_pop(state, 1);
                            if(!binding) {
                                m_api_error = binding.error();
                                break;
                            }
                            if(table == 1)
                                keys.push_back(std::get<Input::Key>(binding.value().control));
                            else
                                buttons.push_back(
                                    std::get<Input::GamepadButton>(binding.value().control));
                        }
                        if(!m_api_error.empty())
                            break;
                    }
                    if(m_api_error.empty()) {
                        auto loaded = m_services.load_input();
                        if(!loaded)
                            m_api_error = loaded.error();
                        else {
                            m_edit.reset(m_services.input_actions, loaded.value(), keys);
                            m_reserved_buttons = std::move(buttons);
                            m_edit_active = true;
                            ++m_edit_revision;
                        }
                    }
                }
                lua_pushboolean(state, m_api_error.empty());
                lua_pushlstring(state, m_api_error.data(), m_api_error.size());
                return 2;
            }
            case Api::InputEnd:
                if(!m_edit.waiting()) {
                    m_edit.clear();
                    m_edit_active = false;
                    m_pending_capture.reset();
                    ++m_edit_revision;
                    m_runtime->set_capture_active(false);
                }
                return 0;
            case Api::InputStatus:
                lua_newtable(state);
                lua_pushboolean(state, m_edit_active);
                lua_setfield(state, -2, "active");
                lua_pushboolean(state, m_edit.waiting());
                lua_setfield(state, -2, "waiting");
                lua_pushboolean(state, bool(m_edit.capture()));
                lua_setfield(state, -2, "capturing");
                lua_pushboolean(state, bool(m_edit.resolution()));
                lua_setfield(state, -2, "compatible");
                lua_pushboolean(
                    state, m_edit.resolution() && !m_edit.resolution().value().issues.empty());
                lua_setfield(state, -2, "has_issues");
                lua_pushlstring(state, m_edit.error().data(), m_edit.error().size());
                lua_setfield(state, -2, "error");
                lua_pushinteger(state, static_cast<lua_Integer>(m_edit_revision));
                lua_setfield(state, -2, "revision");
                return 1;
            case Api::InputActions:
                snapshot_actions();
                lua_newtable(state);
                for(std::size_t index = 0; index < m_actions.size(); ++index) {
                    const auto& action = m_actions[index];
                    lua_newtable(state);
                    lua_pushstring(state, action.id.c_str());
                    lua_setfield(state, -2, "id");
                    lua_pushstring(state, action.name.c_str());
                    lua_setfield(state, -2, "name");
                    lua_pushboolean(state, action.disabled);
                    lua_setfield(state, -2, "disabled");
                    lua_pushboolean(state, action.compatible);
                    lua_setfield(state, -2, "compatible");
                    lua_newtable(state);
                    for(std::size_t row_index = 0; row_index < action.bindings.size();
                        ++row_index) {
                        const auto& row = action.bindings[row_index];
                        lua_newtable(state);
                        lua_pushstring(state, row.id.c_str());
                        lua_setfield(state, -2, "id");
                        lua_pushstring(state, row.source.c_str());
                        lua_setfield(state, -2, "source");
                        lua_pushstring(state, row.control.c_str());
                        lua_setfield(state, -2, "control");
                        lua_pushboolean(state, row.disabled);
                        lua_setfield(state, -2, "disabled");
                        lua_rawseti(state, -2, static_cast<lua_Integer>(row_index + 1));
                    }
                    lua_setfield(state, -2, "bindings");
                    lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
                }
                return 1;
            case Api::InputRestore:
                m_edit.restore_all();
                ++m_edit_revision;
                return 0;
            case Api::InputRestoreBinding:
            case Api::InputToggleBinding:
            case Api::InputCapture: {
                const auto* action_text = luaL_checkstring(state, 1);
                const auto* binding_text = luaL_checkstring(state, 2);
                const auto* kind = operation == Api::InputCapture ? luaL_checkstring(state, 3) : "";
                const auto action = Uuid::parse(action_text), binding = Uuid::parse(binding_text);
                if(!action || !binding)
                    return luaL_error(state, "Expected action and binding UUIDs");
                if(operation == Api::InputRestoreBinding)
                    m_edit.restore_binding(*action, *binding);
                else if(operation == Api::InputToggleBinding)
                    m_edit.disable_binding(
                        *action, *binding, !m_edit.binding_patch(*action, *binding).disabled);
                else {
                    if(std::string_view(kind) != "key" && std::string_view(kind) != "pad")
                        return luaL_error(state, "Capture kind must be key or pad");
                    m_pending_capture = PendingCapture{*action, *binding,
                        std::string_view(kind) == "key"
                            ? PlayerInputEdit::CaptureKind::Keyboard
                            : PlayerInputEdit::CaptureKind::GamepadButton};
                    m_runtime->stop_dispatch_preserve_focus();
                }
                ++m_edit_revision;
                return 0;
            }
            case Api::InputApply:
                m_edit.apply();
                ++m_edit_revision;
                m_runtime->stop_dispatch_preserve_focus();
                return 0;
            default:
                return 0;
        }
    }

    Result<ProjectUi::FrameResult, Error> ProjectUi::Impl::frame(
        const Input::Frame& input, FrameInfo info) {
        using Frame = Result<FrameResult, Error>;
        m_input = &input;
        m_info = info;
        if(!std::isfinite(m_info.fps))
            m_info.fps = 0;
        const ScopeExit finish([this] { m_input = nullptr; });
        m_blocked = m_controller->m_modal;
        const bool was_capturing = bool(m_edit.capture());
        if(auto result = call("on_frame"); !result)
            return Frame::failure({result.error()});
        m_runtime->set_capture_active(was_capturing);
        const bool dispatched_modal = m_controller->m_modal;
        m_dispatching = true;
        m_runtime->process_input(input, m_controller->m_modal, m_info.view);
        m_dispatching = false;
        if(!m_event_error.empty())
            return Frame::failure({std::exchange(m_event_error, {})});
        // UI 生命周期事件由验证阶段隔离，运行时事件在物理帧内顺序交付。
        auto events = std::move(m_events);
        m_events.clear();
        for(const auto& arguments : events) {
            if(auto result = call("on_event", arguments); !result)
                return Frame::failure({result.error()});
        }
        if(was_capturing) {
            const auto* focused = m_runtime->context().GetFocusElement();
            const bool owns_focus = focused
                                    && focused->GetOwnerDocument() == m_controller->m_document
                                    && focused->GetId() == m_capture_focus;
            bool reserved = false;
            const auto& capture = m_edit.capture();
            if(capture && capture->gamepad && input.focused && owns_focus
                && input.serial > capture->serial) {
                for(const auto button : m_reserved_buttons)
                    reserved |= input.gamepads[*capture->gamepad].button(button).pressed;
            }
            if(reserved) {
                m_edit.cancel_capture();
                m_edit.report_error("This gamepad button is reserved by the UI controller");
            } else
                m_edit.capture_input(
                    input, m_controller->m_modal && owns_focus && !m_runtime->text_input_active());
            if(!m_edit.capture())
                ++m_edit_revision;
        }
        if(m_pending_capture) {
            const auto request = std::exchange(m_pending_capture, {});
            const auto* focused = m_runtime->context().GetFocusElement();
            if(input.focused && focused && focused->GetOwnerDocument() == m_controller->m_document
                && !m_runtime->text_input_active()) {
                m_capture_focus = focused->GetId();
                m_edit.start_capture(request->action, request->binding, input, request->kind);
                ++m_edit_revision;
            }
        }
        m_runtime->set_capture_active(bool(m_edit.capture()));
        if(auto display = std::exchange(m_pending_display, {})) {
            auto applied = Result<void>::success();
            if(const auto* settings = std::get_if<Comet::DisplaySettings>(&*display))
                applied = m_services.apply_display(*settings);
            else if(std::get<DisplayDecision>(*display) == DisplayDecision::Confirm)
                applied = m_services.confirm_display();
            else
                applied = m_services.revert_display();
            if(auto result = call("on_display_result",
                   {Rml::Variant(bool(applied)),
                       Rml::Variant(applied ? std::string{} : applied.error())});
                !result)
                return Frame::failure({result.error()});
        }
        if(auto quality = std::exchange(m_pending_quality, {})) {
            const auto resolved = m_renderer.resolve_quality_settings(*quality);
            auto applied = resolved ? m_services.apply_quality(*quality)
                                    : Result<void>::failure(resolved.error().message);
            if(auto result = call("on_quality_result",
                   {Rml::Variant(bool(applied)),
                       Rml::Variant(applied ? std::string{} : applied.error())});
                !result)
                return Frame::failure({result.error()});
        }
        if(auto audio = std::exchange(m_pending_audio, {})) {
            const auto applied = m_services.apply_audio(*audio);
            if(auto result = call(
                   "on_audio_result", {Rml::Variant(bool(applied)),
                                          Rml::Variant(applied ? std::string{} : applied.error())});
                !result)
                return Frame::failure({result.error()});
        }
        if(auto request = m_edit.take_request()) {
            auto applied = m_services.apply_input
                               ? m_services.apply_input(std::move(*request))
                               : Result<void>::failure("Player input apply service is unavailable");
            (void)m_edit.complete(applied);
            ++m_edit_revision;
            if(auto result = call(
                   "on_input_result", {Rml::Variant(bool(applied)),
                                          Rml::Variant(applied ? std::string{} : applied.error())});
                !result)
                return Frame::failure({result.error()});
        }
        if(std::exchange(m_reload_requested, false)) {
            if(auto result = reload(); !result) {
                LOG_WARN("Project UI reload failed: {}", result.error());
                if(auto reported = call("on_reload_error", {Rml::Variant(result.error())});
                    !reported)
                    return Frame::failure({reported.error()});
            }
        }
        if(auto result = call("on_present"); !result)
            return Frame::failure({result.error()});
        for(const auto& [id, contents] : m_pending_markup) {
            if(!replace_markup(*m_controller, id, contents))
                return Frame::failure({"UI markup target no longer exists: " + id});
        }
        m_pending_markup.clear();
        if(auto result = m_runtime->update(); !result)
            return Frame::failure({result.error()});
        if(std::exchange(m_focus_requested, false))
            focus(*m_controller);
        // 同一物理帧内切换 modal 时同步平台基线；下一帧的新按键可以正常导航。
        if(dispatched_modal != m_controller->m_modal)
            m_runtime->process_input(input, m_controller->m_modal, m_info.view);
        m_blocked |= m_controller->m_modal;
        return Frame::success({m_blocked, m_runtime->pointer_blocked()});
    }

    ProjectUi::ProjectUi(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    ProjectUi::~ProjectUi() = default;
    Result<std::unique_ptr<ProjectUi>, Error> ProjectUi::create(Window& window, Renderer& renderer,
        const Project::UiEntry& entry, RmlContext::Options options, Services services) {
        using Creation = Result<std::unique_ptr<ProjectUi>, Error>;
        if(entry.document.empty() || entry.document.is_absolute()
            || entry.document.extension() != ".rml" || entry.controller.empty()
            || entry.controller.is_absolute() || !entry.controller.string().ends_with(".ui.lua"))
            return Creation::failure({"Invalid project UI entry"});
        for(const auto& path : {entry.document, entry.controller}) {
            if(std::ranges::any_of(path, [](const auto& part) { return part == ".."; }))
                return Creation::failure({"UI entry must remain inside project assets"});
        }
        std::error_code error;
        auto root = std::filesystem::canonical(options.resource_root, error);
        if(error)
            return Creation::failure(
                {"Cannot resolve project UI resource root: " + error.message()});
        auto backend = RmlContext::create(window, renderer, std::move(options));
        if(!backend)
            return Creation::failure(backend.error());
        auto impl = std::make_unique<Impl>(window, renderer, std::move(backend).value(), entry,
            std::move(root), std::move(services));
        if(auto result = impl->initialize(); !result)
            return Creation::failure({result.error()});
        return Creation::success(std::unique_ptr<ProjectUi>(new ProjectUi(std::move(impl))));
    }
    Result<ProjectUi::FrameResult, Error> ProjectUi::frame(
        const Input::Frame& input, FrameInfo info) {
        return m_impl->frame(input, info);
    }
    bool ProjectUi::is_modal() const {
        return m_impl->m_controller->m_modal;
    }
    Result<void> ProjectUi::reload() {
        return m_impl->reload();
    }
    void ProjectUi::deactivate() {
        m_impl->deactivate();
    }
    Result<void, GraphicsError> ProjectUi::render(
        OverlayRecordContext& frame, RenderOutput output) {
        return m_impl->m_runtime->render(frame, output);
    }
    void ProjectUi::release_swapchain_resources() {
        m_impl->m_runtime->release_swapchain_resources();
    }
    Result<void, GraphicsError> ProjectUi::rebuild_swapchain_resources(
        const SwapchainCompatibility& compatibility) {
        return m_impl->m_runtime->rebuild_swapchain_resources(compatibility);
    }
}
