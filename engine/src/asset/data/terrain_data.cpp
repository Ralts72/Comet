#include "asset/data/terrain_data.h"
#include "common/file_io.h"
#include "common/json.h"
#include <bit>
#include <cmath>

namespace Comet {
    Result<void> TerrainData::validate() const {
        if(resolution < 5 || resolution > 1025 || !std::has_single_bit(resolution - 1)
            || chunk_cells < 4 || chunk_cells > 128 || !std::has_single_bit(chunk_cells)
            || chunk_cells > resolution - 1 || (resolution - 1) % chunk_cells
            || albedo_resolution < 16 || albedo_resolution > 2048
            || !std::has_single_bit(albedo_resolution))
            return Result<void>::failure(
                "地形分辨率须为 2^n+1（5～1025）；分块须为 4～128 的 2 次幂且整除网格");
        if((!heights.empty() && heights.size() != size_t(resolution) * resolution)
            || (!heightmap.empty() && !heights.empty()))
            return Result<void>::failure(
                "高度图与内联高度只能选一种；内联高度数量须等于分辨率平方");
        if(!layer_weights.empty() && layer_weights.size() != size_t(resolution) * resolution)
            return Result<void>::failure("材质权重数量须与高度网格一致");
        for(const auto value : layer_weights)
            if(!Math::is_finite(value) || glm::any(glm::lessThan(value, Math::Vec3(0)))
                || glm::any(glm::greaterThan(value, Math::Vec3(1)))
                || std::abs(value.x + value.y + value.z - 1) > .0001f)
                return Result<void>::failure("三层材质权重须为有限的 0～1 数值且合计为 1");
        for(float value : heights)
            if(!std::isfinite(value) || value < 0 || value > 1)
                return Result<void>::failure("地形高度样本须为 0～1");
        for(float value : {width, depth, height_scale, skirt_depth})
            if(!std::isfinite(value) || value <= 0 || value > 100000)
                return Result<void>::failure(
                    "地形尺寸、抬升与裙边深度须为有限正数，最大 100000 米");
        if(!std::isfinite(roughness) || roughness < .045f || roughness > 1)
            return Result<void>::failure("地形粗糙度须为 0.045～1");
        for(const auto& layer : layers)
            if(!Math::is_finite(layer.color) || glm::any(glm::lessThan(layer.color, Math::Vec3(0)))
                || glm::any(glm::greaterThan(layer.color, Math::Vec3(1)))
                || !std::isfinite(layer.normal_scale) || layer.normal_scale < 0
                || layer.normal_scale > 4 || !std::isfinite(layer.roughness) || layer.roughness < 0
                || layer.roughness > 1 || !std::isfinite(layer.tiling) || layer.tiling <= 0
                || layer.tiling > 1024)
                return Result<void>::failure(
                    "地形层需要 0～1 线性颜色／粗糙度、0～4 法线强度与 (0,1024] 的重复次数");
        const auto range = [](float first, float second) {
            return std::isfinite(first) && std::isfinite(second) && first >= 0 && second <= 1
                   && first < second;
        };
        if(!range(slope_start, slope_end) || !range(height_start, height_end))
            return Result<void>::failure("地形坡度与高度混合范围须为递增的 0～1 区间");
        const auto& grass = vegetation;
        if(!std::isfinite(grass.wind_amplitude) || grass.wind_amplitude < 0
            || grass.wind_amplitude > 30 || !std::isfinite(grass.wind_frequency)
            || grass.wind_frequency <= 0 || grass.wind_frequency > 10)
            return Result<void>::failure("植被风动摆角须为 0～30 度，频率须为 (0,10] Hz");
        const auto extension = std::filesystem::path(grass.model).extension();
        if(!grass.model.empty() && extension != ".glb" && extension != ".gltf")
            return Result<void>::failure("自定义植被模型须为静态 .gltf／.glb");
        if(grass.count > 10000 || !std::isfinite(grass.min_height)
            || !std::isfinite(grass.max_height) || grass.min_height <= 0
            || grass.max_height < grass.min_height || grass.max_height > 100
            || !std::isfinite(grass.max_slope) || grass.max_slope < 0 || grass.max_slope >= 90
            || !std::isfinite(grass.min_elevation) || !std::isfinite(grass.max_elevation)
            || grass.min_elevation < 0 || grass.max_elevation > 1
            || grass.min_elevation > grass.max_elevation)
            return Result<void>::failure("植被数量须为 0～10000；检查高度、坡度与海拔范围");
        return Result<void>::success();
    }
    Result<TerrainData> TerrainData::deserialize(std::string_view text, std::string_view source) {
        return Json::deserialize<TerrainData>("terrain", text, source,
            [](Json::Node root, const Json::Context& context) -> Result<TerrainData> {
                using Read = Result<TerrainData>;
                if(auto valid = context.validate_keys(
                       root, {"resolution", "heightmap", "heights", "layer_weights", "width",
                                 "depth", "height_scale", "chunk_cells", "skirt_depth", "collision",
                                 "albedo_resolution", "layers", "slope_start", "slope_end",
                                 "height_start", "height_end", "roughness", "vegetation"});
                    !valid)
                    return Read::failure(valid.error());
                TerrainData result;
                const auto field = [&]<typename T>(Json::Node node, const char* name,
                                       T& target) -> Result<void> {
                    auto read = context.read_optional_field<T>(node, name, "a valid terrain value");
                    if(!read)
                        return Result<void>::failure(read.error());
                    if(read.value())
                        target = *read.value();
                    return Result<void>::success();
                };
                for(const auto& [key, target] : {std::pair{"width", &result.width},
                        {"depth", &result.depth}, {"height_scale", &result.height_scale},
                        {"skirt_depth", &result.skirt_depth}, {"slope_start", &result.slope_start},
                        {"slope_end", &result.slope_end}, {"height_start", &result.height_start},
                        {"height_end", &result.height_end}, {"roughness", &result.roughness}})
                    if(auto read = field(root, key, *target); !read)
                        return Read::failure(read.error());
                for(const auto& [key, target] : {std::pair{"resolution", &result.resolution},
                        {"chunk_cells", &result.chunk_cells},
                        {"albedo_resolution", &result.albedo_resolution}})
                    if(auto read = field(root, key, *target); !read)
                        return Read::failure(read.error());
                if(auto read = field(root, "heightmap", result.heightmap); !read)
                    return Read::failure(read.error());
                if(auto read = field(root, "collision", result.collision); !read)
                    return Read::failure(read.error());
                auto heights = context.find(root, "heights");
                if(!heights)
                    return Read::failure(heights.error());
                if(heights.value()) {
                    auto values = context.array(*heights.value(), "heights");
                    if(!values)
                        return Read::failure(values.error());
                    for(auto value : values.value()) {
                        if(result.heights.size() >= 1025 * 1025)
                            return Read::failure("Too many terrain heights");
                        auto height = context.read_scalar<float>(
                            value, "heights", "a finite normalized height");
                        if(!height)
                            return Read::failure(height.error());
                        result.heights.push_back(height.value());
                    }
                }
                auto weights = context.find(root, "layer_weights");
                if(!weights)
                    return Read::failure(weights.error());
                if(weights.value()) {
                    auto values = context.array(*weights.value(), "layer_weights");
                    if(!values
                        || values.value().size() != size_t(result.resolution) * result.resolution
                        || values.value().size() > 1025 * 1025)
                        return Read::failure("材质权重数量须与高度网格一致");
                    result.layer_weights.reserve(values.value().size());
                    for(auto value : values.value()) {
                        auto parsed =
                            Json::read_vector<Math::Vec3>(value, context, "layer_weights");
                        if(!parsed)
                            return Read::failure(parsed.error());
                        result.layer_weights.push_back(parsed.value());
                    }
                }
                auto layers = context.find(root, "layers");
                if(!layers)
                    return Read::failure(layers.error());
                if(layers.value()) {
                    auto values = context.array(*layers.value(), "layers");
                    if(!values || values.value().size() != 3)
                        return Read::failure("Terrain needs exactly three layers");
                    size_t index = 0;
                    for(auto layer : values.value()) {
                        if(auto valid = context.validate_keys(
                               layer, {"color", "texture", "tiling", "normal_texture",
                                          "roughness_texture", "normal_scale", "roughness"});
                            !valid)
                            return Read::failure(valid.error());
                        auto& output = result.layers[index++];
                        if(auto read = field(layer, "texture", output.texture); !read)
                            return Read::failure(read.error());
                        if(auto read = field(layer, "tiling", output.tiling); !read)
                            return Read::failure(read.error());
                        for(const auto& [key, target] :
                            {std::pair{"normal_texture", &output.normal_texture},
                                {"roughness_texture", &output.roughness_texture}})
                            if(auto read = field(layer, key, *target); !read)
                                return Read::failure(read.error());
                        for(const auto& [key, target] :
                            {std::pair{"normal_scale", &output.normal_scale},
                                {"roughness", &output.roughness}})
                            if(auto read = field(layer, key, *target); !read)
                                return Read::failure(read.error());
                        auto color = context.find(layer, "color");
                        if(!color)
                            return Read::failure(color.error());
                        if(color.value()) {
                            auto value =
                                Json::read_vector<Math::Vec3>(*color.value(), context, "color");
                            if(!value)
                                return Read::failure(value.error());
                            output.color = {value.value()[0], value.value()[1], value.value()[2]};
                        }
                    }
                }
                auto grass = context.find(root, "vegetation");
                if(!grass)
                    return Read::failure(grass.error());
                if(grass.value()) {
                    auto node = *grass.value();
                    if(auto valid = context.validate_keys(
                           node, {"count", "seed", "min_height", "max_height", "max_slope",
                                     "min_elevation", "max_elevation", "model", "wind_amplitude",
                                     "wind_frequency", "flexible_wind"});
                        !valid)
                        return Read::failure(valid.error());
                    auto& value = result.vegetation;
                    if(auto read = field(node, "flexible_wind", value.flexible_wind); !read)
                        return Read::failure(read.error());
                    if(auto read = field(node, "model", value.model); !read)
                        return Read::failure(read.error());
                    if(auto read = field(node, "wind_amplitude", value.wind_amplitude); !read)
                        return Read::failure(read.error());
                    if(auto read = field(node, "wind_frequency", value.wind_frequency); !read)
                        return Read::failure(read.error());
                    for(const auto& [key, target] :
                        {std::pair{"count", &value.count}, {"seed", &value.seed}})
                        if(auto read = field(node, key, *target); !read)
                            return Read::failure(read.error());
                    for(const auto& [key, target] : {std::pair{"min_height", &value.min_height},
                            {"max_height", &value.max_height}, {"max_slope", &value.max_slope},
                            {"min_elevation", &value.min_elevation},
                            {"max_elevation", &value.max_elevation}})
                        if(auto read = field(node, key, *target); !read)
                            return Read::failure(read.error());
                }
                if(auto valid = result.validate(); !valid)
                    return Read::failure(valid.error());
                return Read::success(std::move(result));
            });
    }
    Result<TerrainData> TerrainData::load(const std::filesystem::path& path) {
        return Json::load(TerrainData{}, path);
    }
    Result<std::string> TerrainData::serialize() const {
        if(auto valid = validate(); !valid)
            return Result<std::string>::failure(valid.error());
        Json::Writer out;
        out.begin_object();
        out.field("resolution", uint64_t(resolution));
        out.field("heightmap", heightmap);
        if(!heights.empty()) {
            out.key("heights");
            out.begin_array();
            for(float value : heights)
                out.value(value);
            out.end_array();
        }
        if(!layer_weights.empty()) {
            out.key("layer_weights");
            out.begin_array();
            for(const auto weights : layer_weights)
                Json::write_vector(out, weights);
            out.end_array();
        }
        out.field("width", width);
        out.field("depth", depth);
        out.field("height_scale", height_scale);
        out.field("chunk_cells", uint64_t(chunk_cells));
        out.field("skirt_depth", skirt_depth);
        out.field("collision", collision);
        out.field("albedo_resolution", uint64_t(albedo_resolution));
        out.field("slope_start", slope_start);
        out.field("slope_end", slope_end);
        out.field("height_start", height_start);
        out.field("height_end", height_end);
        out.field("roughness", roughness);
        out.key("layers");
        out.begin_array();
        for(const auto& layer : layers) {
            out.begin_object();
            out.key("color");
            out.begin_array();
            out.value(layer.color.x);
            out.value(layer.color.y);
            out.value(layer.color.z);
            out.end_array();
            out.field("texture", layer.texture);
            out.field("tiling", layer.tiling);
            out.field("normal_texture", layer.normal_texture);
            out.field("roughness_texture", layer.roughness_texture);
            out.field("normal_scale", layer.normal_scale);
            out.field("roughness", layer.roughness);
            out.end_object();
        }
        out.end_array();
        out.key("vegetation");
        out.begin_object();
        out.field("count", uint64_t(vegetation.count));
        out.field("seed", uint64_t(vegetation.seed));
        out.field("model", vegetation.model);
        out.field("wind_amplitude", vegetation.wind_amplitude);
        out.field("wind_frequency", vegetation.wind_frequency);
        out.field("flexible_wind", vegetation.flexible_wind);
        out.field("min_height", vegetation.min_height);
        out.field("max_height", vegetation.max_height);
        out.field("max_slope", vegetation.max_slope);
        out.field("min_elevation", vegetation.min_elevation);
        out.field("max_elevation", vegetation.max_elevation);
        out.end_object();
        out.end_object();
        return std::move(out).finish();
    }
    Result<void> TerrainData::save(const std::filesystem::path& path) const {
        auto text = serialize();
        if(!text)
            return Result<void>::failure(text.error());
        return write_text_file_atomic(path, text.value());
    }
}
