#include "ui/window_state.h"

#include "common/file_io.h"
#include "common/json.h"
#include "core/window.h"

#include <cstdint>
#include <system_error>
#include <utility>

namespace CometEditor {
    Comet::Result<std::optional<WindowState>> WindowState::load(const std::filesystem::path& path) {
        using Result = Comet::Result<std::optional<WindowState>>;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if(error)
            return Result::failure("Cannot inspect editor window state: " + error.message());
        if(!exists)
            return Result::success(std::nullopt);
        auto contents = Comet::read_text_file(path);
        if(!contents)
            return Result::failure(contents.error());

        const auto source = path.string();
        const Comet::Json::Context context("editor window state", source);
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Result::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"width", "height", "maximized"}); !valid)
            return Result::failure(valid.error());
        auto width = context.read_field<int>(root, "width", "a positive integer");
        if(!width)
            return Result::failure(width.error());
        auto height = context.read_field<int>(root, "height", "a positive integer");
        if(!height)
            return Result::failure(height.error());
        auto maximized = context.read_field<bool>(root, "maximized", "a boolean");
        if(!maximized)
            return Result::failure(maximized.error());
        if(width.value() <= 0 || height.value() <= 0)
            return Result::failure(context.error("<root>", "window size must be positive"));
        return Result::success(WindowState{width.value(), height.value(), maximized.value()});
    }

    WindowState WindowState::capture(const Comet::Window& window) {
        const auto size = window.get_restore_size();
        return {static_cast<int>(size.x), static_cast<int>(size.y), window.is_maximized()};
    }

    Comet::Result<void> WindowState::save(const std::filesystem::path& path) const {
        if(width <= 0 || height <= 0)
            return Comet::Result<void>::failure("Editor window size must be positive");
        Comet::Json::Writer writer;
        writer.begin_object();
        writer.field("width", std::int64_t(width));
        writer.field("height", std::int64_t(height));
        writer.field("maximized", maximized);
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents)
            return Comet::Result<void>::failure(contents.error());
        return Comet::write_text_file_atomic(path, contents.value());
    }

    void WindowState::apply_to(Comet::WindowSettings& settings) const {
        if(settings.mode != Comet::WindowMode::Windowed)
            return;
        settings.width = width;
        settings.height = height;
        settings.maximized = maximized;
    }
}
