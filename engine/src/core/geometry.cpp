#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Comet {
    std::optional<Ray> unproject_ray(
        const Math::Mat4& inverse_view_projection, const Math::Vec2 ndc) {
        auto near_point = inverse_view_projection * Math::Vec4(ndc, 0.0f, 1.0f);
        auto far_point = inverse_view_projection * Math::Vec4(ndc, 1.0f, 1.0f);
        if(!Math::is_finite(near_point) || !Math::is_finite(far_point)
            || std::abs(near_point.w) <= 0.000001f || std::abs(far_point.w) <= 0.000001f)
            return std::nullopt;
        near_point /= near_point.w;
        far_point /= far_point.w;
        if(!Math::is_finite(near_point) || !Math::is_finite(far_point))
            return std::nullopt;
        const auto delta = Math::Vec3(far_point - near_point);
        const float length = Math::length(delta);
        if(!std::isfinite(length) || length <= 0.000001f)
            return std::nullopt;
        return Ray{Math::Vec3(near_point), delta / length, length};
    }

    BoundingBox BoundingBox::from_point(const Math::Vec3 point) {
        return {.minimum = point, .maximum = point};
    }

    void BoundingBox::include(const Math::Vec3 point) {
        minimum = {
            std::min(minimum.x, point.x),
            std::min(minimum.y, point.y),
            std::min(minimum.z, point.z),
        };
        maximum = {
            std::max(maximum.x, point.x),
            std::max(maximum.y, point.y),
            std::max(maximum.z, point.z),
        };
    }

    void BoundingBox::include(const BoundingBox& box) {
        minimum = Math::min(minimum, box.minimum);
        maximum = Math::max(maximum, box.maximum);
    }

    bool BoundingBox::is_valid() const {
        return Math::is_finite(minimum) && Math::is_finite(maximum) && minimum.x <= maximum.x
               && minimum.y <= maximum.y && minimum.z <= maximum.z;
    }

    Math::Vec3 BoundingBox::center() const {
        return minimum * 0.5f + maximum * 0.5f;
    }

    Math::Vec3 BoundingBox::size() const {
        return maximum - minimum;
    }

    bool Ray::is_valid() const {
        return Math::is_finite(origin) && Math::is_finite(direction)
               && (direction.x != 0.0f || direction.y != 0.0f || direction.z != 0.0f)
               && std::isfinite(max_parameter) && max_parameter >= 0.0f;
    }

    std::optional<Frustum> Frustum::from_view_projection(const Math::Mat4& view_projection) {
        const auto rows = Math::transpose(view_projection);
        Frustum frustum;
        frustum.m_planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
            rows[3] - rows[1], rows[2], rows[3] - rows[2]};
        for(auto& plane : frustum.m_planes) {
            const float length = Math::length(Math::Vec3(plane));
            if(!Math::is_finite(plane) || !std::isfinite(length) || length == 0)
                return std::nullopt;
            plane /= length;
            if(!Math::is_finite(plane))
                return std::nullopt;
        }
        return frustum;
    }

    bool Frustum::intersects(const BoundingBox& box) const {
        if(!box.is_valid())
            return true;
        for(const auto& plane : m_planes) {
            const Math::Vec3 support{plane.x >= 0 ? box.maximum.x : box.minimum.x,
                plane.y >= 0 ? box.maximum.y : box.minimum.y,
                plane.z >= 0 ? box.maximum.z : box.minimum.z};
            if(Math::dot(Math::Vec3(plane), support) + plane.w >= 0)
                continue;
            double distance = plane.w;
            double magnitude = std::abs(distance);
            for(int axis = 0; axis < 3; ++axis) {
                const double term = double(plane[axis]) * support[axis];
                distance += term;
                magnitude += std::abs(term);
            }
            // 留出浮点误差余量，贴着裁剪面的物体继续提交。
            const double tolerance =
                8 * std::numeric_limits<float>::epsilon() * std::max(1.0, magnitude);
            if(distance < -tolerance)
                return false;
        }
        return true;
    }

    std::optional<BoundingBox> transform_box(const BoundingBox& box, const Math::Mat4& transform) {
        if(!box.is_valid() || transform[0][3] != 0.0f || transform[1][3] != 0.0f
            || transform[2][3] != 0.0f || transform[3][3] != 1.0f) {
            return std::nullopt;
        }

        auto result = BoundingBox::from_point(Math::Vec3(transform[3]));
        for(int axis = 0; axis < 3; ++axis) {
            const auto first = Math::Vec3(transform[axis]) * box.minimum[axis];
            const auto second = Math::Vec3(transform[axis]) * box.maximum[axis];
            result.minimum += Math::min(first, second);
            result.maximum += Math::max(first, second);
        }
        return result.is_valid() ? std::optional(result) : std::nullopt;
    }

    std::optional<float> intersect_ray_box(const Ray& ray, const BoundingBox& box) {
        if(!ray.is_valid() || !box.is_valid()) {
            return std::nullopt;
        }

        double minimum_parameter = 0.0;
        double maximum_parameter = ray.max_parameter;
        for(int axis = 0; axis < 3; ++axis) {
            const double origin = ray.origin[axis];
            const double direction = ray.direction[axis];
            const double minimum = box.minimum[axis];
            const double maximum = box.maximum[axis];
            if(direction == 0.0) {
                if(origin < minimum || origin > maximum) {
                    return std::nullopt;
                }
                continue;
            }

            double first = (minimum - origin) / direction;
            double second = (maximum - origin) / direction;
            if(first > second) {
                std::swap(first, second);
            }
            minimum_parameter = std::max(minimum_parameter, first);
            maximum_parameter = std::min(maximum_parameter, second);
            if(minimum_parameter > maximum_parameter) {
                return std::nullopt;
            }
        }
        return static_cast<float>(minimum_parameter);
    }
    void SpatialIndex::clear() {
        m_bounds.clear();
        m_order.clear();
        m_unbounded.clear();
        m_nodes.clear();
    }
    void SpatialIndex::build(uint32_t first, uint32_t count) {
        auto bounds = *m_bounds[m_order[first]];
        for(uint32_t i = 1; i < count; ++i)
            bounds.include(*m_bounds[m_order[first + i]]);
        const auto node = uint32_t(m_nodes.size());
        m_nodes.push_back({bounds, first, count, 0});
        if(count > 8) {
            const auto size = bounds.size();
            int axis = size.y > size.x ? 1 : 0;
            if(size.z > size[axis])
                axis = 2;
            const auto middle = first + count / 2;
            std::nth_element(m_order.begin() + first, m_order.begin() + middle,
                m_order.begin() + first + count, [&](uint32_t a, uint32_t b) {
                    return m_bounds[a]->center()[axis] < m_bounds[b]->center()[axis];
                });
            build(first, middle - first);
            build(middle, first + count - middle);
            m_nodes[node].count = 0;
        }
        m_nodes[node].escape = uint32_t(m_nodes.size());
    }
    void SpatialIndex::update(std::span<const std::optional<BoundingBox>> bounds) {
        bool topology = bounds.size() != m_bounds.size(), changed = topology;
        if(!topology)
            for(size_t i = 0; i < bounds.size(); ++i) {
                topology |= bool(bounds[i]) != bool(m_bounds[i]);
                changed |= topology
                           || (bounds[i]
                               && (bounds[i]->minimum != m_bounds[i]->minimum
                                   || bounds[i]->maximum != m_bounds[i]->maximum));
            }
        if(!changed)
            return;
        m_bounds.assign(bounds.begin(), bounds.end());
        if(topology) {
            m_order.clear();
            m_unbounded.clear();
            m_nodes.clear();
            for(uint32_t i = 0; i < m_bounds.size(); ++i)
                (m_bounds[i] ? m_order : m_unbounded).push_back(i);
            if(!m_order.empty())
                build(0, uint32_t(m_order.size()));
            return;
        }
        for(size_t i = m_nodes.size(); i-- > 0;) {
            auto& node = m_nodes[i];
            if(node.count) {
                node.bounds = *m_bounds[m_order[node.first]];
                for(uint32_t j = 1; j < node.count; ++j)
                    node.bounds.include(*m_bounds[m_order[node.first + j]]);
            } else {
                node.bounds = m_nodes[i + 1].bounds;
                node.bounds.include(m_nodes[m_nodes[i + 1].escape].bounds);
            }
        }
    }
    uint32_t SpatialIndex::query(const Frustum& frustum, std::vector<uint32_t>& visible) const {
        visible.assign(m_unbounded.begin(), m_unbounded.end());
        uint32_t tests = 0;
        for(uint32_t i = 0; i < m_nodes.size();) {
            const auto& node = m_nodes[i];
            ++tests;
            if(!frustum.intersects(node.bounds)) {
                i = node.escape;
                continue;
            }
            if(!node.count) {
                ++i;
                continue;
            }
            for(uint32_t j = 0; j < node.count; ++j) {
                const auto slot = m_order[node.first + j];
                ++tests;
                if(frustum.intersects(*m_bounds[slot]))
                    visible.push_back(slot);
            }
            ++i;
        }
        std::ranges::sort(visible);
        return tests;
    }

}
