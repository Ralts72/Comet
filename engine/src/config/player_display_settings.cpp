#include "config/player_display_settings.h"

#include "common/file_io.h"
#include "common/player_settings_path.h"

namespace Comet {
    Result<PlayerDisplaySettings> PlayerDisplaySettings::load(
        Uuid project_id, DisplaySettings defaults) {
        auto directory = player_settings_directory(project_id);
        if(!directory)
            return Result<PlayerDisplaySettings>::failure(directory.error());
        return load(project_id, defaults, directory.value() / "display.json");
    }

    Result<PlayerDisplaySettings> PlayerDisplaySettings::load(
        Uuid project_id, DisplaySettings defaults, const std::filesystem::path& path) {
        using Loaded = Result<PlayerDisplaySettings>;
        if(!project_id || path.empty())
            return Loaded::failure("Display settings require a project ID and storage path");
        if(auto valid = defaults.validate(); !valid)
            return Loaded::failure(valid.error());
        PlayerDisplaySettings settings;
        settings.m_project_id = project_id;
        settings.m_settings = defaults;
        settings.m_path = path;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if(error)
            return Loaded::failure("Cannot inspect display settings: " + error.message());
        if(!exists)
            return Loaded::success(std::move(settings));
        auto contents = read_text_file(path);
        if(!contents)
            return Loaded::failure(contents.error());
        const auto source = path.string();
        const Json::Context context("player display settings", source);
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Loaded::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "project_id", "display"}); !valid)
            return Loaded::failure(valid.error());
        auto version = context.read_field<std::uint32_t>(root, "version", "version 1");
        auto id = context.read_field<std::string>(root, "project_id", "a project UUID");
        if(!version)
            return Loaded::failure(version.error());
        if(!id)
            return Loaded::failure(id.error());
        if(version.value() != 1 || Uuid::parse(id.value()) != std::optional<Uuid>(project_id))
            return Loaded::failure(context.error("<root>", "version or project ID mismatch"));
        auto display = context.required_child(root, "display");
        if(!display)
            return Loaded::failure(display.error());
        auto loaded = DisplaySettings::read(display.value(), context, "display");
        if(!loaded)
            return Loaded::failure(loaded.error());
        settings.m_settings = loaded.value();
        return Loaded::success(std::move(settings));
    }

    Result<void> PlayerDisplaySettings::save(DisplaySettings settings) {
        if(auto valid = settings.validate(); !valid)
            return valid;
        Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(1));
        writer.field("project_id", m_project_id.to_string());
        writer.key("display");
        settings.write(writer);
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents)
            return Result<void>::failure(contents.error());
        if(auto saved = write_text_file_atomic(m_path, contents.value()); !saved)
            return saved;
        m_settings = settings;
        return Result<void>::success();
    }

    Result<void> PlayerDisplaySettings::save_and_apply(DisplaySettings settings,
        const std::function<Result<void>(const DisplaySettings&)>& apply) {
        if(!apply)
            return Result<void>::failure("Display apply service is unavailable");
        if(auto saved = save(settings); !saved)
            return saved;
        if(auto applied = apply(settings); !applied)
            return Result<void>::failure(
                "Display settings saved but not applied: " + applied.error());
        return Result<void>::success();
    }
}
