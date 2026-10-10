#include "config/display_settings.h"
#include "core/frame_pacer.h"

namespace Comet {
    Result<void> DisplaySettings::validate() const {
        if(width <= 0 || height <= 0)
            return Result<void>::failure("Window dimensions must be positive integers");
        if(mode_name(mode).empty())
            return Result<void>::failure("Unknown window mode");
        if(auto valid = FramePacer::validate_limit(frame_rate_limit); !valid)
            return valid;
        return output.validate();
    }

    Result<WindowMode> DisplaySettings::parse_mode(std::string_view name) {
        if(name == "windowed")
            return Result<WindowMode>::success(WindowMode::Windowed);
        if(name == "borderless")
            return Result<WindowMode>::success(WindowMode::Borderless);
        if(name == "fullscreen")
            return Result<WindowMode>::success(WindowMode::Fullscreen);
        return Result<WindowMode>::failure("Unknown window mode: " + std::string(name));
    }

    std::string_view DisplaySettings::mode_name(WindowMode mode) {
        switch(mode) {
            case WindowMode::Windowed:
                return "windowed";
            case WindowMode::Borderless:
                return "borderless";
            case WindowMode::Fullscreen:
                return "fullscreen";
        }
        return {};
    }

    Result<DisplaySettings> DisplaySettings::read(
        Json::Node node, const Json::Context& context, std::string_view location) {
        return read(node, context, location, DisplaySettings{});
    }

    Result<DisplaySettings> DisplaySettings::read(Json::Node node, const Json::Context& context,
        std::string_view location, const DisplaySettings& defaults) {
        using Read = Result<DisplaySettings>;
        if(auto valid = context.validate_keys(
               node, {"width", "height", "mode", "vsync", "output", "frame_rate_limit"}, location);
            !valid)
            return Read::failure(valid.error());
        auto width = context.read_field<int>(node, "width", "a positive integer", location);
        auto height = context.read_field<int>(node, "height", "a positive integer", location);
        auto mode = context.read_field<std::string>(node, "mode", "a window mode", location);
        auto vsync = context.read_field<bool>(node, "vsync", "a boolean", location);
        if(!width)
            return Read::failure(width.error());
        if(!height)
            return Read::failure(height.error());
        if(!mode)
            return Read::failure(mode.error());
        if(!vsync)
            return Read::failure(vsync.error());
        auto parsed_mode = parse_mode(mode.value());
        if(!parsed_mode)
            return Read::failure(context.error(location, parsed_mode.error()));
        DisplaySettings result{width.value(), height.value(), parsed_mode.value(), vsync.value()};
        result.output = defaults.output;
        result.frame_rate_limit = defaults.frame_rate_limit;
        Json::Node limit;
        if(!node["frame_rate_limit"].get(limit)) {
            auto loaded = context.read_scalar<int>(
                limit, std::string(location) + ".frame_rate_limit", "an integer from 0 to 1000");
            if(!loaded)
                return Read::failure(loaded.error());
            result.frame_rate_limit = loaded.value();
        }
        Json::Node output;
        if(!node["output"].get(output)) {
            auto loaded = OutputSettings::read(output, context, std::string(location) + ".output");
            if(!loaded)
                return Read::failure(loaded.error());
            result.output = loaded.value();
        }
        if(auto valid = result.validate(); !valid)
            return Read::failure(context.error(location, valid.error()));
        return Read::success(result);
    }

    void DisplaySettings::write(Json::Writer& writer) const {
        writer.begin_object();
        writer.field("width", static_cast<std::int64_t>(width));
        writer.field("height", static_cast<std::int64_t>(height));
        writer.field("mode", mode_name(mode));
        writer.field("vsync", vsync);
        writer.field("frame_rate_limit", static_cast<std::int64_t>(frame_rate_limit));
        writer.key("output");
        output.write(writer);
        writer.end_object();
    }
}
