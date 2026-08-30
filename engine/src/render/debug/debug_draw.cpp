#include "render/debug/debug_draw.h"

#include <array>
#include <cmath>
#include <limits>

namespace Comet {
    namespace {
        bool is_finite(const Math::Vec3 value) {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        bool is_finite(const Math::Vec4 value) {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z)
                && std::isfinite(value.w);
        }
    }

    bool DebugDrawList::add_line(
        const Math::Vec3 start,
        const Math::Vec3 end,
        const Math::Vec4 color) {
        if(!is_finite(start) || !is_finite(end) || !is_finite(color)) {
            return false;
        }
        if(m_vertices.size()
           > std::numeric_limits<std::size_t>::max() - 2) {
            return false;
        }
        m_vertices.push_back({.position = start, .color = color});
        m_vertices.push_back({.position = end, .color = color});
        return true;
    }

    bool DebugDrawList::add_box(
        const AxisAlignedBox& box,
        const Math::Vec4 color) {
        if(!box.is_valid() || !is_finite(color)) {
            return false;
        }
        constexpr std::size_t BOX_VERTEX_COUNT = 24;
        if(m_vertices.size()
           > std::numeric_limits<std::size_t>::max() - BOX_VERTEX_COUNT) {
            return false;
        }

        const std::array corners{
            Math::Vec3(box.minimum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.maximum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.maximum.z)
        };
        constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        }};

        m_vertices.reserve(m_vertices.size() + BOX_VERTEX_COUNT);
        for(const auto& [start, end]: edges) {
            if(!add_line(corners[start], corners[end], color)) {
                return false;
            }
        }
        return true;
    }

    void DebugDrawList::reserve_lines(const std::size_t line_count) {
        if(line_count
           > std::numeric_limits<std::size_t>::max() / 2) {
            return;
        }
        m_vertices.reserve(line_count * 2);
    }
}
