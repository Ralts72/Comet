#include "file_watch_config.h"

#include "common/file_io.h"

#include <string>
#include <yaml-cpp/yaml.h>

namespace CometEditor {
    namespace {
        constexpr int MAX_QUIET_PERIOD_MS = 2000;
    }

    Comet::Result<std::chrono::milliseconds> parse_file_watch_quiet_period(
        const std::string_view yaml) {
        using Result = Comet::Result<std::chrono::milliseconds>;
        YAML::Node root;
        try {
            root = YAML::Load(std::string(yaml));
        } catch(const YAML::Exception& error) {
            return Result::failure(error.what());
        }
        if(!root || root.IsNull())
            return Result::success(DEFAULT_FILE_WATCH_QUIET_PERIOD);
        if(!root.IsMap())
            return Result::failure("Expected a config mapping");
        const YAML::Node editor = root["editor"];
        if(!editor)
            return Result::success(DEFAULT_FILE_WATCH_QUIET_PERIOD);
        if(!editor.IsMap())
            return Result::failure("editor must be a mapping");
        const YAML::Node watch = editor["file_watch"];
        if(!watch)
            return Result::success(DEFAULT_FILE_WATCH_QUIET_PERIOD);
        if(!watch.IsMap())
            return Result::failure("editor.file_watch must be a mapping");
        const YAML::Node quiet_period = watch["quiet_period_ms"];
        if(!quiet_period)
            return Result::success(DEFAULT_FILE_WATCH_QUIET_PERIOD);
        int milliseconds = 0;
        if(!quiet_period.IsScalar() || !YAML::convert<int>::decode(quiet_period, milliseconds)
            || milliseconds < 0 || milliseconds > MAX_QUIET_PERIOD_MS) {
            return Result::failure("editor.file_watch.quiet_period_ms must be an integer from 0 to "
                                   + std::to_string(MAX_QUIET_PERIOD_MS));
        }
        return Result::success(std::chrono::milliseconds(milliseconds));
    }

    Comet::Result<std::chrono::milliseconds> load_file_watch_quiet_period(
        const std::filesystem::path& path) {
        using Result = Comet::Result<std::chrono::milliseconds>;
        auto text = Comet::read_text_file(path);
        if(!text)
            return Result::failure(
                "Cannot read editor file-watch config '" + path.string() + "': " + text.error());
        auto parsed = parse_file_watch_quiet_period(text.value());
        if(!parsed)
            return Result::failure(
                "Invalid editor file-watch config '" + path.string() + "': " + parsed.error());
        return parsed;
    }
}
