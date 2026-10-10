#include "input/player_input_settings.h"

#include "common/file_io.h"
#include "common/json.h"
#include "common/player_settings_path.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        constexpr std::uint32_t FORMAT_VERSION = 2;

        Result<std::optional<std::string>> read_optional_file(const std::filesystem::path& path) {
            using Read = Result<std::optional<std::string>>;
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if(error)
                return Read::failure("Cannot inspect player input settings '" + path.string()
                                     + "': " + error.message());
            if(!exists)
                return Read::success(std::nullopt);
            auto contents = read_text_file(path);
            if(!contents)
                return Read::failure(contents.error());
            return Read::success(std::move(contents).value());
        }

        Result<Uuid> read_id(Json::Node node, std::string_view key, const Json::Context& context,
            std::string_view location) {
            const auto text =
                context.read_field<std::string>(node, key, "a non-zero UUID", location);
            if(!text)
                return Result<Uuid>::failure(text.error());
            const auto id = Uuid::parse(text.value());
            if(!id || !*id)
                return Result<Uuid>::failure(
                    context.error(location, std::string(key) + " must be a non-zero UUID"));
            return Result<Uuid>::success(*id);
        }

        Result<InputActions::Type> read_type(
            Json::Node node, const Json::Context& context, std::string_view location) {
            using Type = InputActions::Type;
            const auto text = context.read_field<std::string>(node, "type", "a string", location);
            if(!text)
                return Result<Type>::failure(text.error());
            if(text.value() == "button")
                return Result<Type>::success(Type::Button);
            if(text.value() == "axis")
                return Result<Type>::success(Type::Axis);
            if(text.value() == "delta")
                return Result<Type>::success(Type::Delta);
            return Result<Type>::failure(context.error(location, "unknown action type"));
        }

        Result<bool> read_disabled(
            Json::Node node, const Json::Context& context, const std::string& location) {
            Json::Node disabled;
            if(node["disabled"].get(disabled))
                return Result<bool>::success(false);
            return context.read_scalar<bool>(disabled, location + ".disabled", "a boolean");
        }

        Result<InputOverrides::Binding> read_binding(
            Json::Node node, const Json::Context& context, const std::string& location) {
            using Read = Result<InputOverrides::Binding>;
            if(auto valid = context.validate_keys(
                   node, {"id", "source", "control", "scale", "deadzone", "disabled"}, location);
                !valid)
                return Read::failure(valid.error());
            const auto id = read_id(node, "id", context, location);
            if(!id)
                return Read::failure(id.error());
            const auto disabled = read_disabled(node, context, location);
            if(!disabled)
                return Read::failure(disabled.error());
            InputOverrides::Binding binding{.id = id.value(), .disabled = disabled.value()};
            Json::Node source;
            Json::Node control;
            const bool has_source = !node["source"].get(source);
            const bool has_control = !node["control"].get(control);
            if(has_source != has_control)
                return Read::failure(
                    context.error(location, "source and control must be specified together"));
            if(has_source) {
                const auto source_name =
                    context.read_scalar<std::string>(source, location + ".source", "a string");
                const auto control_name =
                    context.read_scalar<std::string>(control, location + ".control", "a string");
                if(!source_name)
                    return Read::failure(source_name.error());
                if(!control_name)
                    return Read::failure(control_name.error());
                const auto parsed =
                    InputActions::parse_binding(source_name.value(), control_name.value());
                if(!parsed)
                    return Read::failure(context.error(location, parsed.error()));
                binding.control = parsed.value().control;
            }
            for(const auto& [key, target] :
                {std::pair{"scale", &binding.scale}, std::pair{"deadzone", &binding.deadzone}}) {
                Json::Node field;
                if(node[key].get(field))
                    continue;
                const auto value =
                    context.read_scalar<float>(field, location + "." + key, "a finite number");
                if(!value)
                    return Read::failure(value.error());
                *target = value.value();
            }
            return Read::success(std::move(binding));
        }

        Result<InputOverrides> read_overrides(Json::Node root, const Json::Context& context) {
            const auto child = context.required_child(root, "actions");
            if(!child)
                return Result<InputOverrides>::failure(child.error());
            const auto entries = context.array(child.value(), "actions");
            if(!entries)
                return Result<InputOverrides>::failure(entries.error());
            std::vector<InputOverrides::Action> actions;
            for(const auto entry : entries.value()) {
                const auto location = "actions[" + std::to_string(actions.size()) + "]";
                if(auto valid = context.validate_keys(
                       entry, {"id", "type", "disabled", "bindings"}, location);
                    !valid)
                    return Result<InputOverrides>::failure(valid.error());
                const auto id = read_id(entry, "id", context, location);
                const auto type = read_type(entry, context, location);
                const auto disabled = read_disabled(entry, context, location);
                if(!id)
                    return Result<InputOverrides>::failure(id.error());
                if(!type)
                    return Result<InputOverrides>::failure(type.error());
                if(!disabled)
                    return Result<InputOverrides>::failure(disabled.error());
                InputOverrides::Action action{id.value(), type.value(), disabled.value(), {}};
                Json::Node bindings;
                if(!entry["bindings"].get(bindings)) {
                    const auto items = context.array(bindings, location + ".bindings");
                    if(!items)
                        return Result<InputOverrides>::failure(items.error());
                    for(const auto item : items.value()) {
                        const auto field =
                            location + ".bindings[" + std::to_string(action.bindings.size()) + "]";
                        auto binding = read_binding(item, context, field);
                        if(!binding)
                            return Result<InputOverrides>::failure(binding.error());
                        action.bindings.push_back(std::move(binding).value());
                        if(action.bindings.size() > InputActions::MAX_BINDINGS)
                            return Result<InputOverrides>::failure(
                                context.error(field, "too many binding overrides"));
                    }
                }
                actions.push_back(std::move(action));
                if(actions.size() > InputActions::MAX_ACTIONS)
                    return Result<InputOverrides>::failure(
                        context.error(location, "too many action overrides"));
            }
            auto result = InputOverrides::create(std::move(actions));
            if(!result)
                return Result<InputOverrides>::failure(context.error("actions", result.error()));
            return result;
        }

        Result<std::string> serialize(Uuid project_id, const InputOverrides& overrides) {
            Json::Writer writer;
            writer.begin_object();
            writer.field("version", std::uint64_t(FORMAT_VERSION));
            writer.field("project_id", project_id.to_string());
            writer.key("actions");
            writer.begin_array();
            for(const auto& action : overrides.actions()) {
                writer.begin_object();
                writer.field("id", action.id.to_string());
                switch(action.type) {
                    case InputActions::Type::Button:
                        writer.field("type", "button");
                        break;
                    case InputActions::Type::Axis:
                        writer.field("type", "axis");
                        break;
                    case InputActions::Type::Delta:
                        writer.field("type", "delta");
                        break;
                }
                if(action.disabled)
                    writer.field("disabled", true);
                if(!action.bindings.empty()) {
                    writer.key("bindings");
                    writer.begin_array();
                    for(const auto& binding : action.bindings) {
                        writer.begin_object();
                        writer.field("id", binding.id.to_string());
                        if(binding.disabled)
                            writer.field("disabled", true);
                        if(binding.control) {
                            const auto name = InputActions::format_binding({*binding.control});
                            if(!name)
                                return Result<std::string>::failure(name.error());
                            writer.field("source", name.value().source);
                            writer.field("control", name.value().control);
                        }
                        if(binding.scale)
                            writer.field("scale", *binding.scale);
                        if(binding.deadzone)
                            writer.field("deadzone", *binding.deadzone);
                        writer.end_object();
                    }
                    writer.end_array();
                }
                writer.end_object();
            }
            writer.end_array();
            writer.end_object();
            return std::move(writer).finish();
        }
    }

    PlayerInputSettings::PlayerInputSettings(Uuid project_id, std::filesystem::path path)
        : m_project_id(project_id), m_path(std::move(path)) {}

    Result<std::filesystem::path> PlayerInputSettings::default_path(Uuid project_id) {
        auto directory = player_settings_directory(project_id);
        if(!directory)
            return directory;
        return Result<std::filesystem::path>::success(directory.value() / "input.json");
    }

    Result<PlayerInputSettings> PlayerInputSettings::load(Uuid project_id) {
        const auto path = default_path(project_id);
        if(!path)
            return Result<PlayerInputSettings>::failure(path.error());
        return load(project_id, path.value());
    }

    Result<PlayerInputSettings> PlayerInputSettings::load(
        Uuid project_id, const std::filesystem::path& path) {
        using Loaded = Result<PlayerInputSettings>;
        if(!project_id)
            return Loaded::failure("Player input settings require a non-zero project ID");
        if(path.empty())
            return Loaded::failure("Player input settings path cannot be empty");
        std::error_code error;
        auto absolute = std::filesystem::absolute(path, error);
        if(error)
            return Loaded::failure("Cannot resolve player input settings path: " + error.message());
        PlayerInputSettings settings(project_id, std::move(absolute));
        auto contents = read_optional_file(settings.m_path);
        if(!contents)
            return Loaded::failure(contents.error());
        if(!contents.value())
            return Loaded::success(std::move(settings));
        const auto source = settings.m_path.string();
        const Json::Context context("player input settings", source);
        simdjson::dom::parser parser;
        const auto parsed = context.parse(parser, *contents.value());
        if(!parsed)
            return Loaded::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "project_id", "actions"}); !valid)
            return Loaded::failure(valid.error());
        const auto version =
            context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
        if(!version)
            return Loaded::failure(version.error());
        if(version.value() != FORMAT_VERSION)
            return Loaded::failure(context.error("version", "unsupported version"));
        const auto id = read_id(root, "project_id", context, "<root>");
        if(!id)
            return Loaded::failure(id.error());
        if(id.value() != project_id)
            return Loaded::failure(context.error("project_id", "does not match the project"));
        auto overrides = read_overrides(root, context);
        if(!overrides)
            return Loaded::failure(overrides.error());
        settings.m_overrides = std::move(overrides).value();
        return Loaded::success(std::move(settings));
    }

    Result<void> PlayerInputSettings::save(InputOverrides overrides) {
        if(overrides == m_overrides)
            return Result<void>::success();
        auto contents = serialize(m_project_id, overrides);
        if(!contents)
            return Result<void>::failure(contents.error());
        if(auto saved = write_text_file_atomic(m_path, contents.value()); !saved)
            return saved;
        m_overrides = std::move(overrides);
        return Result<void>::success();
    }

    Result<void> PlayerInputSettings::save_and_apply(const InputActions& defaults,
        InputOverrides overrides,
        const std::function<Result<void, Error>(InputActions)>& apply_actions) {
        if(!apply_actions)
            return Result<void>::failure("Player input settings require a runtime apply callback");
        auto resolved = overrides.resolve(defaults);
        if(!resolved)
            return Result<void>::failure(resolved.error());
        if(auto saved = save(std::move(overrides)); !saved)
            return saved;
        if(auto applied = apply_actions(std::move(resolved).value().actions); !applied)
            return Result<void>::failure(
                "Player settings saved but not applied: " + applied.error().message);
        return Result<void>::success();
    }
}
