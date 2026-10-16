#include "asset/irradiance_bake.h"

#include "asset/data/cubemap_sampling.h"

#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Comet::IrradianceBake {
    namespace {
        constexpr std::size_t MAX_TRIANGLES = 1'000'000;
        constexpr std::size_t MAX_TEXTURE_BYTES = 256 * 1024 * 1024;
        constexpr float RAY_BIAS = 0.001f;

        std::uint32_t scramble(std::uint32_t value) {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            return value ^ (value >> 16);
        }

        float random_float(std::uint32_t& state) {
            state += 0x9e3779b9u;
            return static_cast<float>(scramble(state) >> 8) / 16777216.0f;
        }

        Math::Vec3 sphere_direction(
            const std::uint32_t sample, const std::uint32_t count, const float rotation) {
            const float y = 1 - 2 * (sample + 0.5f) / count;
            const double phase = sample * 0.6180339887498949 + rotation;
            const float angle = static_cast<float>(2 * Math::PI * (phase - std::floor(phase)));
            const float radius = std::sqrt(std::max(0.0f, 1 - y * y));
            return {radius * std::cos(angle), y, radius * std::sin(angle)};
        }

        Math::Vec3 cosine_direction(const Math::Vec3 normal, std::uint32_t& state) {
            const float radius = std::sqrt(random_float(state));
            const float angle = 2 * Math::PI * random_float(state);
            Math::Vec3 reference{0, 0, 1};
            if(std::abs(normal.z) > 0.9f)
                reference = {0, 1, 0};
            const auto tangent = glm::normalize(glm::cross(reference, normal));
            return tangent * (radius * std::cos(angle))
                   + glm::cross(normal, tangent) * (radius * std::sin(angle))
                   + normal * std::sqrt(std::max(0.0f, 1 - radius * radius));
        }

        struct TextureImage {
            std::uint32_t width = 0, height = 0;
            SamplerDesc sampler;
            std::vector<Math::Vec4> pixels;

            static Result<TextureImage> decode(
                const TextureData& source, const std::size_t remaining_bytes) {
                using Decoded = Result<TextureImage>;
                if(source.cubemap || source.width <= 0 || source.height <= 0 || source.width > 8192
                    || source.height > 8192 || source.mip_levels == 0)
                    return Decoded::failure("Invalid irradiance bake texture dimensions");
                std::size_t texel_size = 0;
                if(source.format == Format::R8G8B8A8_UNORM
                    || source.format == Format::R8G8B8A8_SRGB)
                    texel_size = 4;
                else if(source.format == Format::R16G16B16A16_SFLOAT)
                    texel_size = 8;
                else if(source.format == Format::R32G32B32A32_SFLOAT)
                    texel_size = 16;
                if(texel_size == 0)
                    return Decoded::failure("Unsupported irradiance bake texture format");
                const auto count = std::size_t(source.width) * source.height;
                if(source.pixels.size() < count * texel_size
                    || count > remaining_bytes / sizeof(Math::Vec4))
                    return Decoded::failure(
                        "Irradiance bake texture exceeds its payload or memory budget");
                TextureImage result{static_cast<std::uint32_t>(source.width),
                    static_cast<std::uint32_t>(source.height),
                    source.sampler.value_or(SamplerDesc{})};
                result.pixels.resize(count);
                for(std::size_t index = 0; index < count; ++index) {
                    auto& pixel = result.pixels[index];
                    const auto* bytes = source.pixels.data() + index * texel_size;
                    if(texel_size == 4) {
                        for(int channel = 0; channel < 4; ++channel)
                            pixel[channel] = bytes[channel] / 255.0f;
                        if(source.format == Format::R8G8B8A8_SRGB) {
                            for(int channel = 0; channel < 3; ++channel) {
                                const float value = pixel[channel];
                                pixel[channel] = value <= 0.04045f
                                                     ? value / 12.92f
                                                     : std::pow((value + 0.055f) / 1.055f, 2.4f);
                            }
                        }
                    } else if(texel_size == 8) {
                        glm::u16vec4 packed;
                        std::memcpy(&packed, bytes, 8);
                        pixel = glm::unpackHalf(packed);
                    } else {
                        std::memcpy(&pixel, bytes, 16);
                    }
                    if(!Math::is_finite(pixel) || glm::any(glm::lessThan(pixel, Math::Vec4(0))))
                        return Decoded::failure(
                            "Irradiance bake texture contains invalid radiance");
                }
                return Decoded::success(std::move(result));
            }

            Math::Vec4 texel(int x, int y) const {
                const auto address = [](int& value, const int size, const SamplerAddressMode mode) {
                    if(mode == SamplerAddressMode::ClampToBorder && (value < 0 || value >= size))
                        return false;
                    if(mode == SamplerAddressMode::Repeat)
                        value = (value % size + size) % size;
                    else if(mode == SamplerAddressMode::MirrorRepeat) {
                        value = (value % (2 * size) + 2 * size) % (2 * size);
                        if(value >= size)
                            value = 2 * size - 1 - value;
                    } else
                        value = std::clamp(value, 0, size - 1);
                    return true;
                };
                if(!address(x, int(width), sampler.address_mode_u)
                    || !address(y, int(height), sampler.address_mode_v))
                    return {0, 0, 0, 1};
                return pixels[std::size_t(y) * width + x];
            }

            Math::Vec4 sample(Math::Vec2 uv) const {
                // 先约化周期坐标，避免有限但极大的源 UV 转整数时溢出。
                for(int axis = 0; axis < 2; ++axis) {
                    const auto mode = axis == 0 ? sampler.address_mode_u : sampler.address_mode_v;
                    if(mode == SamplerAddressMode::Repeat)
                        uv[axis] -= std::floor(uv[axis]);
                    else if(mode == SamplerAddressMode::MirrorRepeat)
                        uv[axis] = std::fmod(std::fmod(uv[axis], 2.0f) + 2.0f, 2.0f);
                    else
                        uv[axis] = std::clamp(uv[axis], -1.0f, 2.0f);
                }
                const auto p = uv * Math::Vec2(width, height) - 0.5f;
                if(sampler.mag_filter == Filter::Nearest)
                    return texel(int(std::floor(p.x + 0.5f)), int(std::floor(p.y + 0.5f)));
                const int x = int(std::floor(p.x)), y = int(std::floor(p.y));
                return glm::mix(glm::mix(texel(x, y), texel(x + 1, y), p.x - x),
                    glm::mix(texel(x, y + 1), texel(x + 1, y + 1), p.x - x), p.y - y);
            }
        };

        struct Triangle {
            std::array<Math::Vec3, 3> positions;
            std::array<Math::Vec3, 3> normals;
            std::array<Math::Vec2, 3> texcoords;
            Math::Vec3 geometric_normal;
            std::uint32_t material = 0;
            std::uint32_t order = 0;

            BoundingBox bounds() const {
                auto box = BoundingBox::from_point(positions[0]);
                box.include(positions[1]);
                box.include(positions[2]);
                return box;
            }
            Math::Vec3 center() const {
                return (positions[0] + positions[1] + positions[2]) / 3.0f;
            }
        };

        struct Node {
            BoundingBox bounds;
            std::uint32_t first = 0, count = 0, right = 0;
        };

        struct Hit {
            std::uint32_t triangle = 0;
            float distance = 0;
            Math::Vec3 barycentric{};
        };

        class Baker {
        public:
            Baker(const Input& input, const Settings& settings, const Progress& progress)
                : m_input(input), m_settings(settings), m_progress(progress) {}

            Result<Output> run() {
                const auto start = std::chrono::steady_clock::now();
                if(auto checked = prepare(); !checked)
                    return Result<Output>::failure(checked.error());
                auto& data = m_output.data;
                const auto total = static_cast<std::uint32_t>(data.probes.size());
                for(std::uint32_t index = 0; index < total; ++index) {
                    m_completed = index;
                    if(cancelled() || !bake_probe(index))
                        return Result<Output>::failure("Irradiance bake cancelled");
                }
                m_completed = total;
                if(cancelled())
                    return Result<Output>::failure("Irradiance bake cancelled");
                if(auto checked = data.validate(); !checked)
                    return Result<Output>::failure(checked.error());
                auto& stats = m_output.statistics;
                stats.seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                stats.triangles = static_cast<std::uint32_t>(m_triangles.size());
                stats.bvh_nodes = static_cast<std::uint32_t>(m_nodes.size());
                stats.working_bytes = m_triangles.capacity() * sizeof(Triangle)
                                      + m_nodes.capacity() * sizeof(Node) + m_texture_bytes
                                      + data.probes.size() * sizeof(IrradianceProbe)
                                      + (m_environment ? m_environment->decoded_bytes() : 0);
                return Result<Output>::success(std::move(m_output));
            }

        private:
            bool cancelled() const {
                return m_progress
                       && !m_progress(
                           m_completed, static_cast<std::uint32_t>(m_output.data.probes.size()));
            }

            Result<void> prepare() {
                auto dimensions = grid_dimensions(m_settings);
                if(!dimensions)
                    return Result<void>::failure(dimensions.error());
                auto& data = m_output.data;
                const auto size = m_settings.bounds.size();
                data.dimensions = dimensions.value();
                const std::size_t count =
                    std::size_t(data.dimensions.x) * data.dimensions.y * data.dimensions.z;
                data.spacing = size / Math::Vec3(data.dimensions);
                data.origin = m_settings.bounds.minimum + data.spacing * 0.5f;
                data.max_distance = m_settings.max_distance;
                data.probes.resize(count);
                if(auto checked = data.validate(); !checked)
                    return checked;
                if(!std::isfinite(m_input.environment_intensity)
                    || m_input.environment_intensity < 0
                    || !std::isfinite(m_input.environment_rotation))
                    return Result<void>::failure("Invalid irradiance environment settings");
                const float angle = Math::radians(Math::wrap_degrees(m_input.environment_rotation));
                m_environment_sine = std::sin(angle);
                m_environment_cosine = std::cos(angle);
                for(const auto& material : m_input.materials) {
                    if(!Math::is_finite(material.base_color) || !Math::is_finite(material.emissive)
                        || glm::any(glm::lessThan(material.base_color, Math::Vec4(0)))
                        || glm::any(glm::greaterThan(material.base_color, Math::Vec4(1)))
                        || glm::any(glm::lessThan(material.emissive, Math::Vec3(0)))
                        || !std::isfinite(material.metallic) || material.metallic < 0
                        || material.metallic > 1 || !material.surface.valid())
                        return Result<void>::failure("Invalid irradiance bake material");
                    for(const auto& texture : {material.base_color_texture,
                            material.metallic_roughness_texture, material.emissive_texture}) {
                        if(auto loaded = add_texture(texture); !loaded)
                            return loaded;
                    }
                }
                if(m_input.environment) {
                    auto checked = CpuCubeSampler::validate(*m_input.environment);
                    if(!checked)
                        return Result<void>::failure(checked.error());
                    if(checked.value() > MAX_TEXTURE_BYTES - m_texture_bytes)
                        return Result<void>::failure(
                            "Irradiance bake environment exceeds its decoded texture budget");
                    m_environment = std::make_unique<CpuCubeSampler>(*m_input.environment);
                }
                m_lighting = LightingData::prepare(m_input.lights);
                if(m_lighting.invalid_lights > 0 || m_lighting.excess_lights > 0)
                    return Result<void>::failure(
                        "Irradiance bake requires valid lights within the runtime light budget");
                if(auto geometry = prepare_geometry(); !geometry)
                    return geometry;
                if(cancelled())
                    return Result<void>::failure("Irradiance bake cancelled");
                if(!m_triangles.empty()) {
                    m_nodes.reserve(m_triangles.size());
                    build_node(0, static_cast<std::uint32_t>(m_triangles.size()));
                }
                return Result<void>::success();
            }

            Result<void> add_texture(const std::shared_ptr<const TextureData>& data) {
                if(!data)
                    return Result<void>::success();
                if(data->cubemap)
                    return Result<void>::failure(
                        "Irradiance bake texture has the wrong dimensionality");
                if(m_textures.contains(data.get()))
                    return Result<void>::success();
                auto decoded = TextureImage::decode(*data, MAX_TEXTURE_BYTES - m_texture_bytes);
                if(!decoded)
                    return Result<void>::failure(decoded.error());
                m_texture_bytes += decoded.value().pixels.size() * sizeof(Math::Vec4);
                m_textures.emplace(data.get(), std::move(decoded).value());
                return Result<void>::success();
            }

            Result<void> prepare_geometry() {
                for(const auto& object : m_input.objects) {
                    if(!object.mesh || object.material >= m_input.materials.size())
                        return Result<void>::failure("Irradiance bake mesh or material is missing");
                    const auto& mesh = *object.mesh;
                    if(m_input.materials[object.material].surface.alpha_mode == AlphaMode::Blend)
                        continue;
                    if(!mesh.skin.empty())
                        return Result<void>::failure(
                            "Skinned meshes are not static irradiance bake inputs");
                    const auto& transform = object.transform;
                    for(int column = 0; column < 4; ++column)
                        if(!Math::is_finite(transform[column]))
                            return Result<void>::failure("Irradiance bake transform is not finite");
                    if(transform[0].w != 0 || transform[1].w != 0 || transform[2].w != 0
                        || transform[3].w != 1)
                        return Result<void>::failure(
                            "Irradiance bake requires affine object transforms");
                    const float determinant = glm::determinant(Math::Mat3(transform));
                    if(!std::isfinite(determinant) || std::abs(determinant) < 1e-10f)
                        return Result<void>::failure("Irradiance bake transform is singular");
                    const auto normal_matrix = glm::transpose(glm::inverse(Math::Mat3(transform)));
                    const auto count =
                        mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
                    if(count % 3 != 0 || count / 3 > MAX_TRIANGLES - m_triangles.size())
                        return Result<void>::failure(
                            "Irradiance bake requires triangles within the 1M triangle budget");
                    for(std::size_t vertex = 0; vertex < count; vertex += 3) {
                        Triangle triangle;
                        triangle.material = object.material;
                        triangle.order = static_cast<std::uint32_t>(m_triangles.size());
                        for(std::size_t corner = 0; corner < 3; ++corner) {
                            const auto index = mesh.indices.empty() ? vertex + corner
                                                                    : mesh.indices[vertex + corner];
                            if(index >= mesh.vertices.size())
                                return Result<void>::failure(
                                    "Irradiance bake mesh index is out of bounds");
                            const auto& source = mesh.vertices[index];
                            if(!Math::is_finite(source.position) || !Math::is_finite(source.normal)
                                || !Math::is_finite(source.texcoord))
                                return Result<void>::failure(
                                    "Irradiance bake mesh contains invalid vertex data");
                            triangle.positions[corner] =
                                Math::Vec3(transform * Math::Vec4(source.position, 1));
                            triangle.normals[corner] = normal_matrix * source.normal;
                            triangle.texcoords[corner] = source.texcoord;
                            if(!Math::is_finite(triangle.positions[corner])
                                || !Math::is_finite(triangle.normals[corner]))
                                return Result<void>::failure(
                                    "Irradiance bake mesh transform overflow");
                        }
                        const auto normal = Math::normalized_direction(
                            glm::cross(triangle.positions[1] - triangle.positions[0],
                                triangle.positions[2] - triangle.positions[0]));
                        if(!normal)
                            continue;
                        triangle.geometric_normal = *normal;
                        const auto sum =
                            triangle.normals[0] + triangle.normals[1] + triangle.normals[2];
                        if(glm::dot(sum, triangle.geometric_normal) < 0)
                            triangle.geometric_normal *= -1;
                        m_triangles.push_back(triangle);
                        if((vertex & 1023u) == 0 && cancelled())
                            return Result<void>::failure("Irradiance bake cancelled");
                    }
                }
                return Result<void>::success();
            }

            std::uint32_t build_node(const std::uint32_t first, const std::uint32_t count) {
                const auto index = static_cast<std::uint32_t>(m_nodes.size());
                auto bounds = m_triangles[first].bounds();
                auto centers = BoundingBox::from_point(m_triangles[first].center());
                for(std::uint32_t i = 1; i < count; ++i) {
                    bounds.include(m_triangles[first + i].bounds());
                    centers.include(m_triangles[first + i].center());
                }
                m_nodes.push_back({bounds, first, count, 0});
                if(count <= 4)
                    return index;
                const auto extent = centers.size();
                int axis = extent.y > extent.x ? 1 : 0;
                if(extent.z > extent[axis])
                    axis = 2;
                const auto middle = first + count / 2;
                std::nth_element(m_triangles.begin() + first, m_triangles.begin() + middle,
                    m_triangles.begin() + first + count,
                    [axis](const Triangle& a, const Triangle& b) {
                        const auto left = a.center()[axis], right = b.center()[axis];
                        return left == right ? a.order < b.order : left < right;
                    });
                m_nodes[index].count = 0;
                build_node(first, middle - first);
                m_nodes[index].right = build_node(middle, first + count - middle);
                return index;
            }

            Math::Vec4 texture(
                const std::shared_ptr<const TextureData>& source, const Math::Vec2 uv) const {
                if(!source)
                    return Math::Vec4(1);
                return m_textures.at(source.get()).sample(uv);
            }

            Math::Vec2 texcoord(const Triangle& triangle, const Math::Vec3 barycentric) const {
                return triangle.texcoords[0] * barycentric.x + triangle.texcoords[1] * barycentric.y
                       + triangle.texcoords[2] * barycentric.z;
            }

            std::optional<Hit> intersect(
                const Math::Vec3 origin, const Math::Vec3 direction, const float maximum) {
                ++m_output.statistics.rays;
                if(m_nodes.empty())
                    return std::nullopt;
                std::array<std::uint32_t, 64> pending{};
                std::size_t size = 1;
                std::optional<Hit> closest;
                float distance = maximum;
                while(size > 0) {
                    const auto& node = m_nodes[pending[--size]];
                    if(!intersect_ray_box({origin, direction, distance}, node.bounds))
                        continue;
                    if(node.count == 0) {
                        pending[size++] = node.right;
                        pending[size++] = static_cast<std::uint32_t>(&node - m_nodes.data()) + 1;
                        continue;
                    }
                    for(std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                        const auto& triangle = m_triangles[i];
                        const auto edge1 = triangle.positions[1] - triangle.positions[0];
                        const auto edge2 = triangle.positions[2] - triangle.positions[0];
                        const auto p = glm::cross(direction, edge2);
                        const float determinant = glm::dot(edge1, p);
                        if(std::abs(determinant) < 1e-9f)
                            continue;
                        const float inverse = 1 / determinant;
                        const auto translated = origin - triangle.positions[0];
                        const float u = glm::dot(translated, p) * inverse;
                        if(u < 0 || u > 1)
                            continue;
                        const auto q = glm::cross(translated, edge1);
                        const float v = glm::dot(direction, q) * inverse;
                        if(v < 0 || u + v > 1)
                            continue;
                        const float t = glm::dot(edge2, q) * inverse;
                        if(t <= RAY_BIAS * 0.25f || t >= distance)
                            continue;
                        const Math::Vec3 barycentric{1 - u - v, u, v};
                        const auto& material = m_input.materials[triangle.material];
                        if(material.surface.alpha_mode == AlphaMode::Mask
                            && material.base_color.a
                                       * texture(material.base_color_texture,
                                           texcoord(triangle, barycentric))
                                           .a
                                   < material.surface.alpha_cutoff)
                            continue;
                        closest = Hit{i, t, barycentric};
                        distance = t;
                    }
                }
                return closest;
            }

            Math::Vec3 environment(Math::Vec3 direction) {
                if(!m_input.environment)
                    return Math::Vec3(0);
                direction = {m_environment_cosine * direction.x - m_environment_sine * direction.z,
                    direction.y,
                    m_environment_sine * direction.x + m_environment_cosine * direction.z};
                return m_environment->sample(direction) * m_input.environment_intensity;
            }

            Math::Vec3 direct_irradiance(const Math::Vec3 position, const Math::Vec3 normal,
                const Math::Vec3 geometric_normal) {
                Math::Vec3 result{0};
                for(std::size_t index = 0; index < static_cast<std::size_t>(m_lighting.light_count);
                    ++index) {
                    const auto& light = m_lighting.lights[index];
                    auto direction = -light.direction;
                    float distance = m_settings.max_distance;
                    float attenuation = 1;
                    if(light.type != static_cast<float>(LightType::Directional)) {
                        const auto delta = light.position - position;
                        const float squared = glm::dot(delta, delta);
                        if(squared < 1e-8f || squared >= light.range * light.range)
                            continue;
                        distance = std::sqrt(squared);
                        direction = delta / distance;
                        const float falloff =
                            std::max(1 - std::pow(distance / light.range, 4.0f), 0.0f);
                        attenuation = falloff * falloff / std::max(squared, 0.01f);
                        if(light.type == static_cast<float>(LightType::Spot)) {
                            const float alignment = glm::dot(-direction, light.direction);
                            if(light.inner_cone_cos - light.outer_cone_cos > 1e-6f) {
                                const float t =
                                    std::clamp((alignment - light.outer_cone_cos)
                                                   / (light.inner_cone_cos - light.outer_cone_cos),
                                        0.0f, 1.0f);
                                attenuation *= t * t * (3 - 2 * t);
                            } else if(alignment < light.outer_cone_cos)
                                attenuation = 0;
                        }
                    }
                    const float cosine = std::max(0.0f, glm::dot(normal, direction));
                    if(cosine <= 0 || attenuation <= 0
                        || glm::dot(geometric_normal, direction) <= 0)
                        continue;
                    if(!intersect(position + geometric_normal * RAY_BIAS, direction,
                           std::max(RAY_BIAS, distance - RAY_BIAS)))
                        result += light.color * (light.intensity * attenuation * cosine);
                }
                return result;
            }

            Math::Vec3 trace(Math::Vec3 origin, Math::Vec3 direction, std::uint32_t& state,
                bool& first_backface) {
                Math::Vec3 result{0}, throughput{1};
                for(std::uint32_t depth = 0;; ++depth) {
                    const auto hit = intersect(origin, direction, m_settings.max_distance);
                    if(!hit) {
                        result += throughput * environment(direction);
                        break;
                    }
                    const auto& triangle = m_triangles[hit->triangle];
                    const auto& material = m_input.materials[triangle.material];
                    auto geometric = triangle.geometric_normal;
                    if(glm::dot(direction, geometric) > 0) {
                        if(!material.surface.double_sided) {
                            first_backface = depth == 0;
                            break;
                        }
                        geometric *= -1;
                    }
                    const auto uv = texcoord(triangle, hit->barycentric);
                    const auto emission =
                        material.emissive * Math::Vec3(texture(material.emissive_texture, uv));
                    result += throughput * emission;
                    if(depth >= m_settings.bounces)
                        break;
                    const auto position = origin + direction * hit->distance;
                    auto normal =
                        Math::normalized_direction(triangle.normals[0] * hit->barycentric.x
                                                   + triangle.normals[1] * hit->barycentric.y
                                                   + triangle.normals[2] * hit->barycentric.z)
                            .value_or(geometric);
                    if(glm::dot(normal, geometric) < 0)
                        normal *= -1;
                    const float metallic = std::clamp(
                        material.metallic * texture(material.metallic_roughness_texture, uv).b,
                        0.0f, 1.0f);
                    const auto diffuse =
                        glm::clamp(Math::Vec3(material.base_color)
                                       * Math::Vec3(texture(material.base_color_texture, uv)),
                            Math::Vec3(0), Math::Vec3(1))
                        * (0.96f * (1 - metallic));
                    result += throughput * diffuse * direct_irradiance(position, normal, geometric)
                              / Math::PI;
                    throughput *= diffuse;
                    if(glm::max(throughput.x, glm::max(throughput.y, throughput.z)) < 1e-5f)
                        break;
                    direction = cosine_direction(normal, state);
                    if(glm::dot(direction, geometric) <= 0)
                        break;
                    origin = position + geometric * RAY_BIAS;
                }
                return result;
            }

            bool bake_probe(const std::uint32_t index) {
                auto& probe = m_output.data.probes[index];
                const auto position = m_output.data.probe_position(index);
                std::uint32_t rotation_state = scramble(m_settings.seed ^ index);
                const float rotation = random_float(rotation_state);
                std::uint32_t backfaces = 0;
                std::array<Math::Vec3, 9> coefficients{};
                for(std::uint32_t sample = 0; sample < m_settings.ray_samples; ++sample) {
                    if((sample & 63u) == 0 && cancelled())
                        return false;
                    auto state =
                        scramble(m_settings.seed ^ (index * 0x85ebca6bu) ^ (sample * 0xc2b2ae35u));
                    const auto direction =
                        sphere_direction(sample, m_settings.ray_samples, rotation);
                    bool backface = false;
                    const auto radiance = trace(position, direction, state, backface);
                    backfaces += backface;
                    const auto harmonics = IrradianceData::basis(direction);
                    for(std::size_t coefficient = 0; coefficient < coefficients.size();
                        ++coefficient)
                        coefficients[coefficient] += radiance * harmonics[coefficient];
                }
                if(backfaces > m_settings.ray_samples / 2)
                    return true;
                constexpr std::array<float, 9> convolution{
                    1, 2.0f / 3, 2.0f / 3, 2.0f / 3, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
                for(std::size_t coefficient = 0; coefficient < coefficients.size(); ++coefficient)
                    probe.coefficients[coefficient] = Math::Vec4(
                        coefficients[coefficient]
                            * (4 * Math::PI / m_settings.ray_samples * convolution[coefficient]),
                        0);
                probe.coefficients[0].w = 1;
                ++m_output.statistics.active_probes;
                for(std::uint32_t y = 0; y < 8; ++y) {
                    if(cancelled())
                        return false;
                    for(std::uint32_t x = 0; x < 8; ++x) {
                        double distance_sum = 0, squared_sum = 0;
                        for(std::uint32_t sub = 0; sub < 4; ++sub) {
                            const Math::Vec2 uv{(x + ((sub & 1u) + 0.5f) / 2) / 8,
                                (y + ((sub >> 1u) + 0.5f) / 2) / 8};
                            const auto hit = intersect(position,
                                IrradianceData::visibility_direction(uv), m_settings.max_distance);
                            const double distance = hit ? hit->distance : m_settings.max_distance;
                            distance_sum += distance;
                            squared_sum += distance * distance;
                        }
                        probe.visibility[y * 8 + x] = {distance_sum / 4, squared_sum / 4};
                    }
                }
                return true;
            }

            const Input& m_input;
            const Settings& m_settings;
            const Progress& m_progress;
            Output m_output;
            LightingData m_lighting;
            std::vector<Triangle> m_triangles;
            std::vector<Node> m_nodes;
            std::unordered_map<const TextureData*, TextureImage> m_textures;
            std::unique_ptr<CpuCubeSampler> m_environment;
            std::size_t m_texture_bytes = 0;
            std::uint32_t m_completed = 0;
            float m_environment_sine = 0, m_environment_cosine = 1;
        };
    }

    Result<Math::Vec3u> grid_dimensions(const Settings& settings) {
        using Grid = Result<Math::Vec3u>;
        if(!settings.bounds.is_valid() || !std::isfinite(settings.spacing) || settings.spacing <= 0
            || !std::isfinite(settings.max_distance) || settings.max_distance <= RAY_BIAS * 2
            || settings.max_distance > 100000 || settings.ray_samples < 32
            || settings.ray_samples > 8192 || settings.bounces > 4)
            return Grid::failure(
                "Invalid irradiance bake bounds, spacing, samples or bounce count");
        const auto size = settings.bounds.size();
        if(glm::any(glm::lessThanEqual(size, Math::Vec3(0))))
            return Grid::failure("Irradiance bake requires a volume with positive extent");
        const auto dimensions = glm::max(glm::ceil(size / settings.spacing), Math::Vec3(2));
        if(!Math::is_finite(dimensions) || glm::any(glm::greaterThan(dimensions, Math::Vec3(64))))
            return Grid::failure("Irradiance bake exceeds 64 probes per axis");
        const Math::Vec3u grid(dimensions);
        if(std::size_t(grid.x) * grid.y * grid.z > IrradianceData::MAX_PROBES)
            return Grid::failure(
                "Irradiance bake exceeds 4096 probes; increase spacing or reduce bounds");
        return Grid::success(grid);
    }

    Result<Output> bake(const Input& input, const Settings& settings, const Progress& progress) {
        return Baker(input, settings, progress).run();
    }
}
