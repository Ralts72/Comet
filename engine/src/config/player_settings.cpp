#include "config/player_settings.h"

#include "common/player_settings_path.h"

namespace Comet {
    namespace {
        struct SettingsNames {
            std::string_view title;
            std::string_view key;
        };

        template<typename T> constexpr SettingsNames settings_names() {
            if constexpr(std::is_same_v<T, AudioSettings>)
                return {"Audio", "audio"};
            else if constexpr(std::is_same_v<T, DisplaySettings>)
                return {"Display", "display"};
            else
                return {"Quality", "quality"};
        }
    }

    template<typename T>
    Result<PlayerSettings<T>> PlayerSettings<T>::load(Uuid project_id, T defaults) {
        auto directory = player_settings_directory(project_id);
        if(!directory)
            return Result<PlayerSettings>::failure(directory.error());
        return load(project_id, defaults,
            directory.value() / (std::string(settings_names<T>().key) + ".json"));
    }

    template<typename T>
    Result<PlayerSettings<T>> PlayerSettings<T>::load(
        Uuid project_id, T defaults, const std::filesystem::path& path) {
        using Loaded = Result<PlayerSettings>;
        constexpr auto names = settings_names<T>();
        if(!project_id || path.empty())
            return Loaded::failure(
                std::string(names.title) + " settings require a project ID and storage path");
        if(auto valid = defaults.validate(); !valid)
            return Loaded::failure(valid.error());
        PlayerSettings settings;
        settings.m_project_id = project_id;
        settings.m_settings = defaults;
        settings.m_path = path;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if(error)
            return Loaded::failure(
                "Cannot inspect " + std::string(names.key) + " settings: " + error.message());
        if(!exists)
            return Loaded::success(std::move(settings));
        auto contents = read_text_file(path);
        if(!contents)
            return Loaded::failure(contents.error());
        auto loaded = Json::deserialize<T>("player " + std::string(names.key) + " settings",
            contents.value(), path.string(), [&](Json::Node root, const Json::Context& context) {
                if(auto valid = context.validate_keys(root, {"version", "project_id", names.key});
                    !valid)
                    return Result<T>::failure(valid.error());
                auto version = context.read_field<std::uint32_t>(root, "version", "version 1");
                auto id = context.read_field<std::string>(root, "project_id", "a project UUID");
                if(!version)
                    return Result<T>::failure(version.error());
                if(!id)
                    return Result<T>::failure(id.error());
                if(version.value() != 1
                    || Uuid::parse(id.value()) != std::optional<Uuid>(project_id))
                    return Result<T>::failure(
                        context.error("<root>", "version or project ID mismatch"));
                auto value = context.required_child(root, names.key);
                if(!value)
                    return Result<T>::failure(value.error());
                if constexpr(std::is_same_v<T, DisplaySettings>)
                    return T::read(value.value(), context, names.key, defaults);
                else
                    return T::read(value.value(), context, names.key);
            });
        if(!loaded)
            return Loaded::failure(loaded.error());
        settings.m_settings = std::move(loaded).value();
        return Loaded::success(std::move(settings));
    }

    template<typename T> Result<void> PlayerSettings<T>::save(T settings) {
        if(auto valid = settings.validate(); !valid)
            return valid;
        constexpr auto names = settings_names<T>();
        auto contents = Json::serialize("player " + std::string(names.key) + " settings", settings,
            [&](const T& value, const Json::Context&, Json::Writer& writer) {
                writer.begin_object();
                writer.field("version", std::uint64_t(1));
                writer.field("project_id", m_project_id.to_string());
                writer.key(names.key);
                value.write(writer);
                writer.end_object();
                return Result<void>::success();
            });
        if(!contents)
            return Result<void>::failure(contents.error());
        if(auto saved = write_text_file_atomic(m_path, contents.value()); !saved)
            return saved;
        m_settings = settings;
        return Result<void>::success();
    }

    template<typename T>
    Result<void> PlayerSettings<T>::save_and_apply(
        T settings, const std::function<Result<void>(const T&)>& apply) {
        const auto title = std::string(settings_names<T>().title);
        if(!apply)
            return Result<void>::failure(title + " apply service is unavailable");
        if(auto saved = save(settings); !saved)
            return saved;
        if(auto applied = apply(settings); !applied)
            return Result<void>::failure(
                title + " settings saved but not applied: " + applied.error());
        return Result<void>::success();
    }

    template class PlayerSettings<AudioSettings>;
    template class PlayerSettings<DisplaySettings>;
    template class PlayerSettings<QualitySettings>;
}
