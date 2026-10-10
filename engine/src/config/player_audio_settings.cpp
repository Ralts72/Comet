#include "config/player_audio_settings.h"

#include "common/file_io.h"
#include "common/player_settings_path.h"

namespace Comet {
    Result<PlayerAudioSettings> PlayerAudioSettings::load(Uuid project_id, AudioSettings defaults) {
        auto directory = player_settings_directory(project_id);
        if(!directory)
            return Result<PlayerAudioSettings>::failure(directory.error());
        return load(project_id, defaults, directory.value() / "audio.json");
    }

    Result<PlayerAudioSettings> PlayerAudioSettings::load(
        Uuid project_id, AudioSettings defaults, const std::filesystem::path& path) {
        using Loaded = Result<PlayerAudioSettings>;
        if(!project_id || path.empty())
            return Loaded::failure("Audio settings require a project ID and storage path");
        if(auto valid = defaults.validate(); !valid)
            return Loaded::failure(valid.error());
        PlayerAudioSettings settings;
        settings.m_project_id = project_id;
        settings.m_settings = defaults;
        settings.m_path = path;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if(error)
            return Loaded::failure("Cannot inspect audio settings: " + error.message());
        if(!exists)
            return Loaded::success(std::move(settings));
        auto contents = read_text_file(path);
        if(!contents)
            return Loaded::failure(contents.error());
        const auto source = path.string();
        const Json::Context context("player audio settings", source);
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Loaded::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "project_id", "audio"}); !valid)
            return Loaded::failure(valid.error());
        auto version = context.read_field<std::uint32_t>(root, "version", "version 1");
        auto id = context.read_field<std::string>(root, "project_id", "a project UUID");
        if(!version)
            return Loaded::failure(version.error());
        if(!id)
            return Loaded::failure(id.error());
        if(version.value() != 1 || Uuid::parse(id.value()) != std::optional<Uuid>(project_id))
            return Loaded::failure(context.error("<root>", "version or project ID mismatch"));
        auto audio = context.required_child(root, "audio");
        if(!audio)
            return Loaded::failure(audio.error());
        auto loaded = AudioSettings::read(audio.value(), context, "audio");
        if(!loaded)
            return Loaded::failure(loaded.error());
        settings.m_settings = loaded.value();
        return Loaded::success(std::move(settings));
    }

    Result<void> PlayerAudioSettings::save(AudioSettings settings) {
        if(auto valid = settings.validate(); !valid)
            return valid;
        Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(1));
        writer.field("project_id", m_project_id.to_string());
        writer.key("audio");
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

    Result<void> PlayerAudioSettings::save_and_apply(
        AudioSettings settings, const std::function<Result<void>(const AudioSettings&)>& apply) {
        if(!apply)
            return Result<void>::failure("Audio apply service is unavailable");
        if(auto saved = save(settings); !saved)
            return saved;
        if(auto applied = apply(settings); !applied)
            return Result<void>::failure(
                "Audio settings saved but not applied: " + applied.error());
        return Result<void>::success();
    }
}
