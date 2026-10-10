#include "render/output_settings.h"

#include <cmath>

namespace Comet {
    Result<void> OutputSettings::validate() const {
        if(mode_name(mode).empty())
            return Result<void>::failure("Unknown output mode");
        if(!std::isfinite(hdr_headroom) || hdr_headroom < 1 || hdr_headroom > 16)
            return Result<void>::failure("HDR headroom must be between 1 and 16");
        if(!std::isfinite(hdr_white_level) || hdr_white_level < 0.5f || hdr_white_level > 2)
            return Result<void>::failure("HDR white level must be between 0.5 and 2");
        return Result<void>::success();
    }

    Result<OutputMode> OutputSettings::parse_mode(std::string_view name) {
        if(name == "sdr")
            return Result<OutputMode>::success(OutputMode::Sdr);
        if(name == "hdr")
            return Result<OutputMode>::success(OutputMode::Hdr);
        if(name == "auto")
            return Result<OutputMode>::success(OutputMode::Auto);
        return Result<OutputMode>::failure("Unknown output mode: " + std::string(name));
    }

    std::string_view OutputSettings::mode_name(OutputMode mode) {
        switch(mode) {
            case OutputMode::Sdr:
                return "sdr";
            case OutputMode::Hdr:
                return "hdr";
            case OutputMode::Auto:
                return "auto";
        }
        return {};
    }

    Result<OutputSettings> OutputSettings::read(
        Json::Node node, const Json::Context& context, std::string_view location) {
        using Read = Result<OutputSettings>;
        if(auto valid =
                context.validate_keys(node, {"mode", "hdr_headroom", "hdr_white_level"}, location);
            !valid)
            return Read::failure(valid.error());
        auto mode = context.read_field<std::string>(node, "mode", "an output mode", location);
        auto headroom = context.read_field<float>(node, "hdr_headroom", "a number", location);
        auto white = context.read_field<float>(node, "hdr_white_level", "a number", location);
        if(!mode)
            return Read::failure(mode.error());
        if(!headroom)
            return Read::failure(headroom.error());
        if(!white)
            return Read::failure(white.error());
        auto parsed = parse_mode(mode.value());
        if(!parsed)
            return Read::failure(context.error(location, parsed.error()));
        OutputSettings settings{parsed.value(), headroom.value(), white.value()};
        if(auto valid = settings.validate(); !valid)
            return Read::failure(context.error(location, valid.error()));
        return Read::success(settings);
    }

    void OutputSettings::write(Json::Writer& writer) const {
        writer.begin_object();
        writer.field("mode", mode_name(mode));
        writer.field("hdr_headroom", hdr_headroom);
        writer.field("hdr_white_level", hdr_white_level);
        writer.end_object();
    }
}
