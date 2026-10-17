#include "asset/data/vector_field_data.h"
#include "common/json.h"
#include <algorithm>

namespace Comet {
    Result<void> VectorFieldData::validate() const {
        if(glm::any(glm::lessThan(dimensions, Math::Vec3u(2)))
            || glm::any(glm::greaterThan(dimensions, Math::Vec3u(32)))
            || values.size() != size_t(dimensions.x) * dimensions.y * dimensions.z)
            return Result<void>::failure("向量场各轴须为 2～32 个样本，数量须与网格一致");
        if(!Math::is_finite(bounds.minimum) || !Math::is_finite(bounds.maximum)
            || !Math::is_finite(bounds.size())
            || glm::any(glm::lessThanEqual(bounds.size(), Math::Vec3(.0001f))))
            return Result<void>::failure("向量场需要有限且非退化的世界空间范围");
        for(const auto value : values)
            if(!Math::is_finite(value)
                || glm::any(glm::greaterThan(glm::abs(value), Math::Vec3(1000))))
                return Result<void>::failure("向量场加速度各分量须为 -1000～1000 m/s²");
        return Result<void>::success();
    }
    Math::Vec3 VectorFieldData::sample(Math::Vec3 position) const {
        if(!Math::is_finite(position) || glm::any(glm::lessThan(position, bounds.minimum))
            || glm::any(glm::greaterThan(position, bounds.maximum)) || values.empty())
            return {};
        const auto grid =
            (position - bounds.minimum) / bounds.size() * Math::Vec3(dimensions - Math::Vec3u(1));
        const auto first = glm::min(Math::Vec3u(grid), dimensions - Math::Vec3u(2));
        const auto blend = grid - Math::Vec3(first);
        Math::Vec3 result(0);
        for(uint32_t z = 0; z < 2; ++z)
            for(uint32_t y = 0; y < 2; ++y)
                for(uint32_t x = 0; x < 2; ++x) {
                    const auto cell = first + Math::Vec3u(x, y, z);
                    const float weight = (x ? blend.x : 1 - blend.x) * (y ? blend.y : 1 - blend.y)
                                         * (z ? blend.z : 1 - blend.z);
                    result +=
                        values[cell.x + dimensions.x * (cell.y + dimensions.y * cell.z)] * weight;
                }
        return result;
    }
    Result<VectorFieldData> VectorFieldData::deserialize(
        std::string_view text, std::string_view source) {
        return Json::deserialize<VectorFieldData>("vector field", text, source,
            [](Json::Node root, const Json::Context& context) -> Result<VectorFieldData> {
                using Read = Result<VectorFieldData>;
                if(auto valid =
                        context.validate_keys(root, {"dimensions", "minimum", "maximum", "values"});
                    !valid)
                    return Read::failure(valid.error());
                VectorFieldData data;
                const auto vector = [&](const char* name) -> Result<Math::Vec3> {
                    auto node = context.required_child(root, name);
                    if(!node)
                        return Result<Math::Vec3>::failure(node.error());
                    return Json::read_vector<Math::Vec3>(node.value(), context, name);
                };
                auto dimensions = vector("dimensions"), minimum = vector("minimum"),
                     maximum = vector("maximum");
                if(!dimensions)
                    return Read::failure(dimensions.error());
                if(!minimum)
                    return Read::failure(minimum.error());
                if(!maximum)
                    return Read::failure(maximum.error());
                if(glm::any(glm::lessThan(dimensions.value(), Math::Vec3(2)))
                    || glm::any(glm::greaterThan(dimensions.value(), Math::Vec3(32)))
                    || dimensions.value() != glm::floor(dimensions.value()))
                    return Read::failure("向量场维度须为 2～32 的整数");
                data.dimensions = Math::Vec3u(dimensions.value());
                data.bounds = {minimum.value(), maximum.value()};
                auto node = context.required_child(root, "values");
                if(!node)
                    return Read::failure(node.error());
                auto values = context.array(node.value(), "values");
                if(!values)
                    return Read::failure(values.error());
                if(values.value().size()
                    != size_t(data.dimensions.x) * data.dimensions.y * data.dimensions.z)
                    return Read::failure("向量场样本数量与维度不一致");
                data.values.reserve(values.value().size());
                for(auto value : values.value()) {
                    auto parsed = Json::read_vector<Math::Vec3>(value, context, "values");
                    if(!parsed)
                        return Read::failure(parsed.error());
                    data.values.push_back(parsed.value());
                }
                if(auto valid = data.validate(); !valid)
                    return Read::failure(valid.error());
                return Read::success(std::move(data));
            });
    }
    Result<VectorFieldData> VectorFieldData::load(const std::filesystem::path& path) {
        std::error_code error;
        const auto bytes = std::filesystem::file_size(path, error);
        if(error || bytes > 8 * 1024 * 1024)
            return Result<VectorFieldData>::failure("无法读取向量场或文件超过 8 MiB");
        auto text = read_text_file(path);
        if(!text)
            return Result<VectorFieldData>::failure(text.error());
        return deserialize(text.value(), path.string());
    }
    Result<std::string> VectorFieldData::serialize() const {
        if(auto valid = validate(); !valid)
            return Result<std::string>::failure(valid.error());
        Json::Writer writer;
        writer.begin_object();
        writer.key("dimensions");
        Json::write_vector(writer, Math::Vec3(dimensions));
        writer.key("minimum");
        Json::write_vector(writer, bounds.minimum);
        writer.key("maximum");
        Json::write_vector(writer, bounds.maximum);
        writer.key("values");
        writer.begin_array();
        for(const auto value : values)
            Json::write_vector(writer, value);
        writer.end_array();
        writer.end_object();
        return std::move(writer).finish();
    }
}
