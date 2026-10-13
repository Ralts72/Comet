#include "render/debug/line_draw_list.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Comet {
    namespace {
        bool is_affine(const Math::Mat4& transform) {
            for(int column = 0; column < 4; ++column)
                if(!Math::is_finite(transform[column]))
                    return false;
            return transform[0][3] == 0.0f && transform[1][3] == 0.0f && transform[2][3] == 0.0f
                   && transform[3][3] == 1.0f;
        }
    }

    bool LineDrawList::add_line(
        const Math::Vec3 start, const Math::Vec3 end, const Math::Vec4 color) {
        if(!Math::is_finite(start) || !Math::is_finite(end) || !Math::is_finite(color)
            || m_vertices.max_size() - m_vertices.size() < 2) {
            return false;
        }
        // 一次插入两个顶点，分配失败时也不留下半条线。
        m_vertices.insert(m_vertices.end(),
            {{.position = start, .color = color}, {.position = end, .color = color}});
        return true;
    }

    bool LineDrawList::add_box(const BoundingBox& box, const Math::Vec4 color) {
        return add_box(box, Math::Mat4(1.0f), color);
    }

    bool LineDrawList::add_box(
        const BoundingBox& box, const Math::Mat4& transform, const Math::Vec4 color) {
        if(!box.is_valid() || !Math::is_finite(color)
            || m_vertices.max_size() - m_vertices.size() < 24) {
            return false;
        }
        if(!is_affine(transform)) {
            return false;
        }
        std::array corners{Math::Vec3(box.minimum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.maximum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.maximum.z)};
        for(auto& corner : corners) {
            corner = Math::Vec3(transform * Math::Vec4(corner, 1.0f));
            if(!Math::is_finite(corner)) {
                return false;
            }
        }
        constexpr std::array<std::array<std::size_t, 2>, 12> edges{{{0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
        std::array<Vertex, 24> vertices;
        std::size_t index = 0;
        for(const auto& [start, end] : edges) {
            vertices[index++] = {.position = corners[start], .color = color};
            vertices[index++] = {.position = corners[end], .color = color};
        }
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        return true;
    }

    bool LineDrawList::add_sphere(
        const float radius, const Math::Mat4& transform, const Math::Vec4 color) {
        constexpr std::size_t segments = 32;
        constexpr std::size_t vertex_count = 3 * segments * 2;
        if(!std::isfinite(radius) || radius <= 0 || !is_affine(transform) || !Math::is_finite(color)
            || m_vertices.max_size() - m_vertices.size() < vertex_count)
            return false;
        static const auto circle = [] {
            std::array<Math::Vec2, segments> points;
            for(std::size_t index = 0; index < segments; ++index) {
                const float angle = 2 * std::numbers::pi_v<float>
                                    * static_cast<float>(index) / static_cast<float>(segments);
                points[index] = {std::cos(angle), std::sin(angle)};
            }
            return points;
        }();
        std::array<Vertex, vertex_count> vertices;
        std::size_t offset = 0;
        for(int axis = 0; axis < 3; ++axis) {
            std::array<Math::Vec3, segments> points;
            for(std::size_t index = 0; index < segments; ++index) {
                Math::Vec3 local(0);
                local[(axis + 1) % 3] = circle[index].x * radius;
                local[(axis + 2) % 3] = circle[index].y * radius;
                points[index] = Math::Vec3(transform * Math::Vec4(local, 1));
                if(!Math::is_finite(points[index]))
                    return false;
            }
            for(std::size_t index = 0; index < segments; ++index) {
                vertices[offset++] = {.position = points[index], .color = color};
                vertices[offset++] = {.position = points[(index + 1) % segments], .color = color};
            }
        }
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        return true;
    }

    bool LineDrawList::add_capsule(const float radius, const float half_height,
        const Math::Mat4& transform, const Math::Vec4 color) {
        constexpr int segments = 32;
        constexpr std::size_t vertex_count = (4 * segments + 4) * 2;
        if(!std::isfinite(radius) || radius <= 0 || !std::isfinite(half_height) || half_height < 0
            || !is_affine(transform) || !Math::is_finite(color)
            || m_vertices.max_size() - m_vertices.size() < vertex_count)
            return false;
        std::array<Vertex, vertex_count> vertices;
        std::size_t offset = 0;
        const auto line = [&](Math::Vec3 first, Math::Vec3 second) {
            for(const auto local : {first, second}) {
                const auto world = Math::Vec3(transform * Math::Vec4(local, 1));
                if(!Math::is_finite(world))
                    return false;
                vertices[offset++] = {.position = world, .color = color};
            }
            return true;
        };
        const auto pi = std::numbers::pi_v<float>;
        for(const float center : {-half_height, half_height}) {
            for(int index = 0; index < segments; ++index) {
                const auto ring = [&](int point) {
                    const auto angle = 2 * pi * point / segments;
                    return Math::Vec3(std::cos(angle) * radius, center, std::sin(angle) * radius);
                };
                if(!line(ring(index), ring(index + 1)))
                    return false;
            }
        }
        for(const int axis : {0, 2}) {
            for(const float sign : {-1.0f, 1.0f}) {
                const auto arc = [&](int point) {
                    const auto angle = pi * point / (segments / 2);
                    Math::Vec3 local{0, sign * (half_height + std::sin(angle) * radius), 0};
                    local[axis] = std::cos(angle) * radius;
                    return local;
                };
                for(int index = 0; index < segments / 2; ++index)
                    if(!line(arc(index), arc(index + 1)))
                        return false;
                Math::Vec3 lower{0, -half_height, 0};
                lower[axis] = sign * radius;
                auto upper = lower;
                upper.y = half_height;
                if(!line(lower, upper))
                    return false;
            }
        }
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        return true;
    }

    bool LineDrawList::append(const LineDrawList& draw_list) {
        const std::size_t count = draw_list.m_vertices.size();
        if(count == 0)
            return true;
        const std::size_t offset = m_vertices.size();
        if(count > m_vertices.max_size() - offset) {
            return false;
        }
        m_vertices.resize(offset + count);
        // resize 后再取源地址，也支持把列表自身追加一次。
        std::copy_n(draw_list.m_vertices.begin(), count, m_vertices.begin() + offset);
        return true;
    }
}
