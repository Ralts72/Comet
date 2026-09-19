#include "render/resource/environment.h"
#include "render/resource/resource_factory.h"
#include "asset/data/environment_data.h"

#include <array>

namespace Comet {
    Result<std::shared_ptr<Environment>, GraphicsError> Environment::try_create(
        RenderResourceFactory& resources, const EnvironmentData& data) {
        using Creation = Result<std::shared_ptr<Environment>, GraphicsError>;
        auto result = std::make_shared<Environment>();
        const std::array inputs{&data.background, &data.irradiance, &data.specular, &data.brdf};
        const std::array outputs{
            &result->background, &result->irradiance, &result->specular, &result->brdf};
        for(size_t i = 0; i < inputs.size(); ++i) {
            auto texture = resources.try_create_texture(*inputs[i]);
            if(!texture)
                return Creation::failure(texture.error());
            *outputs[i] = std::move(texture).value();
        }
        return Creation::success(std::move(result));
    }
}
