#include "project/project_session.h"

#include "common/file_io.h"
#include "common/json.h"

#include <cstdint>
#include <string>
#include <system_error>
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
        std::error_code error;
        const bool exists = std::filesystem::exists(m_file, error);
        if(error)
            return Result::failure("Cannot inspect editor session: " + error.message());
        if(!exists)
            return Result::success();

        auto contents = Comet::read_text_file(m_file);
        if(!contents)
            return Result::failure(contents.error());
        const Comet::Json::Context context("editor session", m_file.string());
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Result::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "scene"}); !valid)
            return Result::failure(valid.error());
        auto version = context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
        if(!version)
            return Result::failure(version.error());
        if(version.value() != FORMAT_VERSION)
            return Result::failure(context.error("version", "unsupported version"));
        auto scene = context.read_field<std::string>(root, "scene", "a path string");
        if(!scene)
            return Result::failure(scene.error());
        auto validated = validate_scene(std::filesystem::path(scene.value()));
        if(!validated)
            return Result::failure(context.error("scene", validated.error()));
        m_last_scene = std::move(validated).value();
        return Result::success();
    }

    Comet::Result<void> ProjectSession::record_scene(const std::filesystem::path& path) {
        using Result = Comet::Result<void>;
        auto validated = validate_scene(path);
        if(!validated)
            return Result::failure(validated.error());
        const auto candidate = std::move(validated).value();
        if(m_last_scene && *m_last_scene == candidate)
            return Result::success();

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
        m_last_scene = candidate;
        return Result::success();
    }
}
