#include "project/project_session.h"

#include "common/file_io.h"
#include "common/json.h"

#include <cstdint>
#include <string>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t FORMAT_VERSION = 1;
    }

    ProjectSession::ProjectSession(Comet::ProjectPaths paths)
        : m_paths(std::move(paths)), m_file(m_paths.editor_state() / "session.json") {}

    Comet::Result<std::filesystem::path> ProjectSession::validate_scene(
        const std::filesystem::path& path) const {
        using Result = Comet::Result<std::filesystem::path>;
        if(path.empty())
            return Result::success({});
        if(path.is_absolute() || path.extension() != ".scene")
            return Result::failure("Last scene must be an assets-relative .scene path");
        if(auto resolved = m_paths.resolve_asset_path(path); !resolved)
            return Result::failure(resolved.error());
        return Result::success(path.lexically_normal());
    }

    Comet::Result<void> ProjectSession::load() {
        using Result = Comet::Result<void>;
        m_last_scene.reset();
        m_save_pending = false;
        auto loaded = Comet::Json::load_optional<std::filesystem::path>("editor session", m_file,
            [this](Comet::Json::Node root, const Comet::Json::Context& context) {
                using ScenePath = Comet::Result<std::filesystem::path>;
                if(auto valid = context.validate_keys(root, {"version", "scene"}); !valid)
                    return ScenePath::failure(valid.error());
                auto version =
                    context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
                if(!version)
                    return ScenePath::failure(version.error());
                if(version.value() != FORMAT_VERSION)
                    return ScenePath::failure(context.error("version", "unsupported version"));
                auto scene = context.read_field<std::string>(root, "scene", "a path string");
                if(!scene)
                    return ScenePath::failure(scene.error());
                auto validated = validate_scene(std::filesystem::path(scene.value()));
                if(!validated)
                    return ScenePath::failure(context.error("scene", validated.error()));
                return validated;
            });
        if(!loaded)
            return Result::failure(loaded.error());
        m_last_scene = std::move(loaded).value();
        return Result::success();
    }

    Comet::Result<void> ProjectSession::record_scene(const std::filesystem::path& path) {
        using Result = Comet::Result<void>;
        auto validated = validate_scene(path);
        if(!validated)
            return Result::failure(validated.error());
        const auto candidate = std::move(validated).value();
        if(m_last_scene && *m_last_scene == candidate && !m_save_pending)
            return Result::success();
        m_last_scene = candidate;
        m_save_pending = true;

        Comet::Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(FORMAT_VERSION));
        writer.field("scene", candidate.generic_string());
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents)
            return Result::failure(contents.error());
        if(auto saved = Comet::write_text_file_atomic(m_file, contents.value()); !saved)
            return saved;
        m_save_pending = false;
        return Result::success();
    }
}
