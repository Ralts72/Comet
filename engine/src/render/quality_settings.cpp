#include "render/quality_settings.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    Result<void> QualitySettings::validate() const {
        if(msaa_samples != 1 && msaa_samples != 2 && msaa_samples != 4 && msaa_samples != 8)
            return Result<void>::failure("MSAA samples must be 1, 2, 4 or 8");
        if(!std::isfinite(max_anisotropy) || max_anisotropy < 1 || max_anisotropy > 16)
            return Result<void>::failure("Anisotropy must be between 1 and 16");
        if(!std::isfinite(render_scale) || render_scale < 0.5f || render_scale > 1)
            return Result<void>::failure("Render scale must be between 0.5 and 1");
        return Result<void>::success();
    }

    Math::Vec2u QualitySettings::scene_size(Math::Vec2u output) const {
        const auto scaled = [&](uint32_t size) {
            return std::max(1u, static_cast<uint32_t>(std::round(double(size) * render_scale)));
        };
        return {scaled(output.x), scaled(output.y)};
    }

    Result<QualitySettings> QualitySettings::read(
        Json::Node node, const Json::Context& context, std::string_view location) {
        using Read = Result<QualitySettings>;
        if(auto valid = context.validate_keys(
               node, {"msaa_samples", "max_anisotropy", "render_scale"}, location);
            !valid)
            return Read::failure(valid.error());
        auto samples =
            context.read_field<uint32_t>(node, "msaa_samples", "a sample count", location);
        auto anisotropy = context.read_field<float>(node, "max_anisotropy", "a number", location);
        auto scale = context.read_field<float>(node, "render_scale", "a number", location);
        if(!samples)
            return Read::failure(samples.error());
        if(!anisotropy)
            return Read::failure(anisotropy.error());
        if(!scale)
            return Read::failure(scale.error());
        QualitySettings settings{samples.value(), anisotropy.value(), scale.value()};
        if(auto valid = settings.validate(); !valid)
            return Read::failure(context.error(location, valid.error()));
        return Read::success(settings);
    }

    void QualitySettings::write(Json::Writer& writer) const {
        writer.begin_object();
        writer.field("msaa_samples", uint64_t(msaa_samples));
        writer.field("max_anisotropy", max_anisotropy);
        writer.field("render_scale", render_scale);
        writer.end_object();
    }
}
