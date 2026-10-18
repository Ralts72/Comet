#include "render/resource/texture.h"
#include "render/resource/irradiance.h"

#include "asset/data/irradiance_data.h"
#include "asset/data/texture_data.h"
#include "render/resource/resource_factory.h"

#include <cstring>
#include <utility>

namespace Comet {
    BoundingBox Irradiance::bounds() const {
        return {origin - spacing * 0.5f, origin + spacing * (Math::Vec3(dimensions) - 0.5f)};
    }

    Result<std::shared_ptr<Irradiance>, GraphicsError> Irradiance::try_create(
        RenderResourceFactory& resources, IrradianceData data) {
        using Created = Result<std::shared_ptr<Irradiance>, GraphicsError>;
        if(auto checked = data.validate(); !checked)
            return Created::failure({checked.error()});
        static_assert(sizeof(IrradianceProbe) == TEXELS_PER_PROBE * sizeof(Math::Vec4));
        TextureData pixels;
        pixels.width = TEXELS_PER_PROBE;
        pixels.height = static_cast<int>(data.probes.size());
        pixels.format = Format::R32G32B32A32_SFLOAT;
        pixels.sampler = SamplerDesc{.mag_filter = Filter::Nearest,
            .min_filter = Filter::Nearest,
            .mipmap_mode = SamplerMipmapMode::Nearest,
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge};
        pixels.pixels.resize(data.probes.size() * sizeof(IrradianceProbe));
        std::memcpy(pixels.pixels.data(), data.probes.data(), pixels.pixels.size());
        auto texture = resources.try_create_texture(std::move(pixels));
        if(!texture)
            return Created::failure(texture.error());
        auto result = std::make_shared<Irradiance>();
        result->origin = data.origin;
        result->spacing = data.spacing;
        result->dimensions = data.dimensions;
        result->probes = std::move(texture).value();
        return Created::success(std::move(result));
    }

}
