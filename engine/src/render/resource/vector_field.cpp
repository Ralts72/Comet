#include "render/resource/texture.h"
#include "render/resource/vector_field.h"
#include "render/resource/resource_factory.h"
#include "asset/data/texture_data.h"
#include <cstring>
namespace Comet {
    Result<std::shared_ptr<VectorField>, GraphicsError> VectorField::try_create(
        RenderResourceFactory& resources, VectorFieldData data) {
        using Created = Result<std::shared_ptr<VectorField>, GraphicsError>;
        if(auto valid = data.validate(); !valid)
            return Created::failure({valid.error()});
        // 逻辑 3D 网格打包为 Y/Z 切片图集，复用普通纹理上传；Shader 显式三线性采样。
        TextureData image;
        image.width = int(data.dimensions.x);
        image.height = int(data.dimensions.y * data.dimensions.z);
        image.format = Format::R32G32B32A32_SFLOAT;
        image.pixels.resize(data.values.size() * sizeof(Math::Vec4));
        for(size_t i = 0; i < data.values.size(); ++i) {
            const Math::Vec4 value(data.values[i], 0);
            std::memcpy(image.pixels.data() + i * sizeof(value), &value, sizeof(value));
        }
        auto uploaded = resources.try_create_texture(std::move(image));
        if(!uploaded)
            return Created::failure(uploaded.error());
        auto result = std::make_shared<VectorField>();
        result->data = std::move(data);
        result->texture = std::move(uploaded).value();
        return Created::success(std::move(result));
    }

}
