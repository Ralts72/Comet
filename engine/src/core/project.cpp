#include "core/project.h"

#include "common/file_io.h"
#include "common/json.h"

#include <system_error>
#include <utility>

namespace Comet {
    namespace {
        Result<InputActions> read_input_actions(Json::Node node, const Json::Context& context) {
            const auto entries = context.array(node, "input_actions");
            if(!entries)
                return Result<InputActions>::failure(entries.error());
            std::vector<InputActions::Action> actions;
            for(const auto entry : entries.value()) {
                const auto location = "input_actions[" + std::to_string(actions.size()) + "]";
                if(auto valid =
                        context.validate_keys(entry, {"name", "type", "bindings"}, location);
                    !valid)
                    return Result<InputActions>::failure(valid.error());
                auto name = context.read_field<std::string>(entry, "name", "a string", location);
                auto type = context.read_field<std::string>(entry, "type", "a string", location);
                if(!name)
                    return Result<InputActions>::failure(name.error());
                if(!type)
                    return Result<InputActions>::failure(type.error());
                InputActions::Action action;
                action.name = std::move(name).value();
                if(type.value() == "button")
                    action.type = InputActions::Type::Button;
                else if(type.value() == "axis")
                    action.type = InputActions::Type::Axis;
                else if(type.value() == "delta")
                    action.type = InputActions::Type::Delta;
                else
                    return Result<InputActions>::failure(
                        context.error(location, "unknown action type"));
                auto child = context.required_child(entry, "bindings", location);
                if(!child)
                    return Result<InputActions>::failure(child.error());
                auto bindings = context.array(child.value(), location + ".bindings");
                if(!bindings)
                    return Result<InputActions>::failure(bindings.error());
                for(const auto binding : bindings.value()) {
                    const auto field =
                        location + ".bindings[" + std::to_string(action.bindings.size()) + "]";
                    if(auto valid = context.validate_keys(
                           binding, {"source", "control", "scale", "deadzone"}, field);
                        !valid)
                        return Result<InputActions>::failure(valid.error());
                    auto source =
                        context.read_field<std::string>(binding, "source", "a string", field);
                    auto control =
                        context.read_field<std::string>(binding, "control", "a string", field);
                    if(!source)
                        return Result<InputActions>::failure(source.error());
                    if(!control)
                        return Result<InputActions>::failure(control.error());
                    float scale = 1;
                    float deadzone = 0;
                    for(const auto& [key, target] :
                        {std::pair{"scale", &scale}, std::pair{"deadzone", &deadzone}}) {
                        Json::Node value;
                        if(!binding[key].get(value)) {
                            auto parsed = context.read_scalar<float>(
                                value, field + "." + key, "a finite number");
                            if(!parsed)
                                return Result<InputActions>::failure(parsed.error());
                            *target = parsed.value();
                        }
                    }
                    auto parsed = InputActions::parse_binding(
                        source.value(), control.value(), scale, deadzone);
                    if(!parsed)
                        return Result<InputActions>::failure(context.error(field, parsed.error()));
                    action.bindings.push_back(std::move(parsed).value());
                    if(action.bindings.size() > 16)
                        return Result<InputActions>::failure(
                            context.error(field, "too many bindings"));
                }
                actions.push_back(std::move(action));
                if(actions.size() > 128)
                    return Result<InputActions>::failure(
                        context.error(location, "too many actions"));
            }
            auto result = InputActions::create(std::move(actions));
            if(!result)
                return Result<InputActions>::failure(
                    context.error("input_actions", result.error()));
            return result;
        }
    }

    Project::Project(ProjectPaths paths) : m_paths(std::move(paths)) {}

    Result<Project> Project::load(const std::filesystem::path& path) {
        if(path.empty())
            return Result<Project>::failure("Project path cannot be empty");
        std::error_code error;
        const bool directory = std::filesystem::is_directory(path, error);
        if(error)
            return Result<Project>::failure(
                "Cannot inspect project path '" + path.string() + "': " + error.message());
        const auto manifest =
            std::filesystem::canonical(directory ? path / "project.json" : path, error);
        if(error)
            return Result<Project>::failure(
                "Cannot resolve project manifest '" + path.string() + "': " + error.message());
        const std::string source = manifest.string();
        const Json::Context context("project", source);
        simdjson::dom::parser parser;
        auto contents = read_text_file(manifest);
        if(!contents)
            return Result<Project>::failure(contents.error());
        const auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Result<Project>::failure(parsed.error());
        const auto data = parsed.value();
        if(auto valid =
                context.validate_keys(data, {"version", "name", "startup_scene", "input_actions"});
            !valid)
            return Result<Project>::failure(valid.error());
        const auto version =
            context.read_field<std::uint32_t>(data, "version", "an unsigned integer");
        if(!version)
            return Result<Project>::failure(version.error());
        if(version.value() != FORMAT_VERSION)
            return Result<Project>::failure(
                context.error("version", "unsupported version " + std::to_string(version.value())
                                             + "; expected " + std::to_string(FORMAT_VERSION)));

        Project project{ProjectPaths(manifest.parent_path())};
        auto name = context.read_field<std::string>(data, "name", "a non-empty string");
        if(!name)
            return Result<Project>::failure(name.error());
        project.m_name = std::move(name).value();
        if(project.m_name.find_first_not_of(" \t\r\n") == std::string::npos)
            return Result<Project>::failure(context.error("name", "name cannot be empty"));
        const bool has_assets = std::filesystem::is_directory(project.paths().assets(), error);
        if(error || !has_assets)
            return Result<Project>::failure(context.error(
                "<root>", error ? error.message() : "assets directory does not exist"));

        Json::Node scene;
        if(!data["startup_scene"].get(scene)) {
            const auto scene_path =
                context.read_scalar<std::string>(scene, "startup_scene", "a string");
            if(!scene_path)
                return Result<Project>::failure(scene_path.error());
            const std::filesystem::path relative(scene_path.value());
            if(!relative.empty()) {
                if(relative.is_absolute() || relative.extension() != ".scene")
                    return Result<Project>::failure(
                        context.error("startup_scene", "expected an assets-relative .scene path"));
                const auto resolved = project.paths().resolve_asset_path(relative);
                if(!resolved)
                    return Result<Project>::failure(
                        context.error("startup_scene", resolved.error()));
                project.m_startup_scene = relative.lexically_normal();
            }
        }
        Json::Node input;
        if(!data["input_actions"].get(input)) {
            auto actions = read_input_actions(input, context);
            if(!actions)
                return Result<Project>::failure(actions.error());
            project.m_input_actions = std::move(actions).value();
        }
        return Result<Project>::success(std::move(project));
    }
}
