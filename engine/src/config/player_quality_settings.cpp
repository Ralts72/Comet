#include "config/player_quality_settings.h"

#include "common/file_io.h"
#include "common/player_settings_path.h"

namespace Comet {
    Result<PlayerQualitySettings> PlayerQualitySettings::load(
        Uuid project_id, QualitySettings defaults) {
        auto directory = player_settings_directory(project_id);
        if(!directory)
            return Result<PlayerQualitySettings>::failure(directory.error());
        return load(project_id, defaults, directory.value() / "quality.json");
    }

    Result<PlayerQualitySettings> PlayerQualitySettings::load(
        Uuid project_id, QualitySettings defaults, const std::filesystem::path& path) {
        using Loaded = Result<PlayerQualitySettings>;
        if(!project_id || path.empty())
            return Loaded::failure("Quality settings require a project ID and storage path");
        if(auto valid = defaults.validate(); !valid)
            return Loaded::failure(valid.error());
        PlayerQualitySettings settings;
        settings.m_project_id = project_id;
        settings.m_settings = defaults;
        settings.m_path = path;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if(error)
            return Loaded::failure("Cannot inspect quality settings: " + error.message());
        if(!exists)
            return Loaded::success(std::move(settings));
        auto contents = read_text_file(path);
        if(!contents)
            return Loaded::failure(contents.error());
        const auto source = path.string();
        const Json::Context context("player quality settings", source);
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Loaded::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "project_id", "quality"}); !valid)
            return Loaded::failure(valid.error());
        auto version = context.read_field<std::uint32_t>(root, "version", "version 1");
        auto id = context.read_field<std::string>(root, "project_id", "a project UUID");
        if(!version)
            return Loaded::failure(version.error());
        if(!id)
            return Loaded::failure(id.error());
        if(version.value() != 1 || Uuid::parse(id.value()) != std::optional<Uuid>(project_id))
            return Loaded::failure(context.error("<root>", "version or project ID mismatch"));
        auto quality = context.required_child(root, "quality");
        if(!quality)
            return Loaded::failure(quality.error());
        auto loaded = QualitySettings::read(quality.value(), context, "quality");
        if(!loaded)
            return Loaded::failure(loaded.error());
        settings.m_settings = loaded.value();
        return Loaded::success(std::move(settings));
    }

    Result<void> PlayerQualitySettings::save(QualitySettings settings) {
        if(auto valid = settings.validate(); !valid)
            return valid;
        Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(1));
        writer.field("project_id", m_project_id.to_string());
        writer.key("quality");
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

    Result<void> PlayerQualitySettings::save_and_apply(QualitySettings settings,
        const std::function<Result<void>(const QualitySettings&)>& apply) {
        if(!apply)
            return Result<void>::failure("Quality apply service is unavailable");
        if(auto saved = save(settings); !saved)
            return saved;
        if(auto applied = apply(settings); !applied)
            return Result<void>::failure(
                "Quality settings saved but not applied: " + applied.error());
        return Result<void>::success();
    }
}
