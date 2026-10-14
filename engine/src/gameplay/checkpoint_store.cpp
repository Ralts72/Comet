#include "gameplay/checkpoint_store.h"
#include "common/json.h"
#include "common/player_settings_path.h"
#include "scene/runtime_session.h"

namespace Comet {
    namespace {
        Result<ParameterValue> read_value(
            Json::Node node, const Json::Context& context, const std::string& location) {
            if(node.is_bool()) {
                auto value = context.read_scalar<bool>(node, location, "a boolean");
                if(!value)
                    return Result<ParameterValue>::failure(value.error());
                return Result<ParameterValue>::success(value.value());
            }
            if(node.is_string()) {
                auto value = context.read_scalar<std::string>(node, location, "a string");
                if(!value)
                    return Result<ParameterValue>::failure(value.error());
                return Result<ParameterValue>::success(std::move(value).value());
            }
            if(node.is_array()) {
                auto value = Json::read_vector<Math::Vec3>(node, context, location);
                if(!value)
                    return Result<ParameterValue>::failure(value.error());
                return Result<ParameterValue>::success(value.value());
            }
            auto value = context.read_scalar<float>(
                node, location, "a finite number, boolean, string or Vec3");
            if(!value)
                return Result<ParameterValue>::failure(value.error());
            return Result<ParameterValue>::success(value.value());
        }

        Result<Checkpoint> read_checkpoint(
            Json::Node root, const Json::Context& context, Uuid project) {
            using Read = Result<Checkpoint>;
            if(auto valid = context.validate_keys(
                   root, {"version", "project", "scene", "checkpoint", "values"});
                !valid)
                return Read::failure(valid.error());
            auto version = context.read_field<uint32_t>(root, "version", "version 1");
            if(!version)
                return Read::failure(version.error());
            if(version.value() != 1)
                return Read::failure(context.error("version", "unsupported checkpoint version"));
            auto identity = context.read_field<std::string>(root, "project", "a project UUID");
            if(!identity)
                return Read::failure(identity.error());
            if(Uuid::parse(identity.value()) != std::optional(project))
                return Read::failure(
                    context.error("project", "checkpoint belongs to another project"));
            auto scene = context.read_field<uint64_t>(root, "scene", "a scene asset handle");
            if(!scene)
                return Read::failure(scene.error());
            auto marker = context.read_field<std::string>(root, "checkpoint", "a checkpoint name");
            if(!marker)
                return Read::failure(marker.error());
            if(!scene.value() || !valid_parameter_name(marker.value()))
                return Read::failure(
                    context.error("checkpoint", "invalid scene or checkpoint name"));
            auto node = context.required_child(root, "values");
            if(!node)
                return Read::failure(node.error());
            auto values = context.object(node.value(), "values");
            if(!values)
                return Read::failure(values.error());
            Checkpoint checkpoint{AssetHandle{scene.value()}, std::move(marker).value(), {}};
            for(const auto field : values.value()) {
                if(checkpoint.values.size() >= 128 || !valid_parameter_name(field.key))
                    return Read::failure(
                        context.error("values", "invalid name or too many saved values"));
                auto value = read_value(field.value, context, "values." + std::string(field.key));
                if(!value)
                    return Read::failure(value.error());
                checkpoint.values.emplace(field.key, std::move(value).value());
            }
            if(!RuntimeSession::valid_values(checkpoint.values))
                return Read::failure(context.error("values", "invalid saved values"));
            return Read::success(std::move(checkpoint));
        }
    }

    GameSession::Persistence CheckpointStore::persistence(
        std::function<AssetHandle(std::string_view)> find_scene,
        std::function<std::optional<std::string>(AssetHandle)> scene_path) const {
        return {.save =
                    [this, find = std::move(find_scene)](
                        std::string_view path, const RuntimeSession::CheckpointRequest& request) {
                        const auto scene = find(path);
                        if(!scene)
                            return Result<void>::failure("Checkpoint scene is not indexed");
                        return save({scene, request.marker, request.values});
                    },
            .load = [this,
                        locate = std::move(scene_path)]() -> Result<RuntimeSession::SceneRequest> {
                using Loaded = Result<RuntimeSession::SceneRequest>;
                auto saved = load();
                if(!saved)
                    return Loaded::failure(saved.error());
                if(!saved.value())
                    return Loaded::failure("No checkpoint has been saved");
                auto& checkpoint = *saved.value();
                auto path = locate(checkpoint.scene);
                if(!path)
                    return Loaded::failure("Saved checkpoint scene is missing");
                return Loaded::success({RuntimeSession::SceneRequest::Kind::Load, std::move(*path),
                    std::move(checkpoint.values), std::move(checkpoint.marker)});
            }};
    }

    Result<std::filesystem::path> CheckpointStore::default_path(Uuid project) {
        auto directory = player_settings_directory(project);
        if(!directory)
            return directory;
        return Result<std::filesystem::path>::success(
            directory.value() / "saves" / "checkpoint.json");
    }

    Result<void> CheckpointStore::save(const Checkpoint& checkpoint) const {
        if(!m_project || !checkpoint.scene || !valid_parameter_name(checkpoint.marker)
            || !RuntimeSession::valid_values(checkpoint.values))
            return Result<void>::failure("Invalid project, scene, checkpoint or progress values");
        Json::Writer writer;
        writer.begin_object();
        writer.field("version", uint64_t{1});
        writer.field("project", m_project.to_string());
        writer.field("scene", checkpoint.scene.value());
        writer.field("checkpoint", checkpoint.marker);
        writer.key("values");
        writer.begin_object();
        for(const auto& [name, value] : checkpoint.values) {
            writer.key(name);
            std::visit(
                [&](const auto& item) {
                    using T = std::remove_cvref_t<decltype(item)>;
                    if constexpr(std::is_same_v<T, Math::Vec3>)
                        Json::write_vector(writer, item);
                    else if constexpr(!std::is_same_v<T, Uuid> && !std::is_same_v<T, Math::Vec4>)
                        writer.value(item);
                },
                value);
        }
        writer.end_object();
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents)
            return Result<void>::failure(contents.error());
        return write_text_file_atomic(m_path, contents.value());
    }

    Result<std::optional<Checkpoint>> CheckpointStore::load() const {
        if(!m_project)
            return Result<std::optional<Checkpoint>>::failure(
                "Checkpoint requires a project identity");
        return Json::load_optional<Checkpoint>(
            "checkpoint", m_path, [this](Json::Node root, const Json::Context& context) {
                return read_checkpoint(root, context, m_project);
            });
    }
}
