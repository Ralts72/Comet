#pragma once
#include "asset/data/vector_field_data.h"
#include "graphics/result.h"
#include <memory>
namespace Comet {
    class RenderResourceFactory;
    struct COMET_API VectorField {
        VectorFieldData data;
        std::shared_ptr<Texture> texture;
        static Result<std::shared_ptr<VectorField>, GraphicsError> try_create(
            RenderResourceFactory&, VectorFieldData);
    };
}
