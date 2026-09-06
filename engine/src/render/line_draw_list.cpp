#include "render/line_draw_list.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Comet {
    bool LineDrawList::add_line(
        const Math::Vec3 start, const Math::Vec3 end, const Math::Vec4 color) {
        if(!Math::is_finite(start) || !Math::is_finite(end) || !Math::is_finite(color)) {
            return false;
        }
        // 一次插入两个顶点，分配失败时也不留下半条线。
        m_vertices.insert(m_vertices.end(),
            {{.position = start, .color = color}, {.position = end, .color = color}});
        return true;
    }

    bool LineDrawList::add_box(const BoundingBox& box, const Math::Vec4 color) {
        if(!box.is_valid() || !Math::is_finite(color)) {
            return false;
        }
        const std::array corners{Math::Vec3(box.minimum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.minimum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.minimum.z),
            Math::Vec3(box.minimum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.minimum.y, box.maximum.z),
            Math::Vec3(box.maximum.x, box.maximum.y, box.maximum.z),
            Math::Vec3(box.minimum.x, box.maximum.y, box.maximum.z)};
        constexpr std::array<std::array<std::size_t, 2>, 12> edges{
            {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4},
                {1, 5}, {2, 6}, {3, 7}}};
        std::array<Vertex, 24> vertices;
        std::size_t index = 0;
        for(const auto& [start, end] : edges) {
            vertices[index++] = {.position = corners[start], .color = color};
            vertices[index++] = {.position = corners[end], .color = color};
        }
        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
        return true;
    }

    void LineDrawList::append(const LineDrawList& draw_list) {
        const std::size_t count = draw_list.m_vertices.size();
        const std::size_t offset = m_vertices.size();
        if(count > m_vertices.max_size() - offset) {
            throw std::length_error("Line draw vertex count exceeds vector capacity");
        }
        m_vertices.resize(offset + count);
        // resize 后再取源地址，也支持把列表自身追加一次。
        std::copy_n(draw_list.m_vertices.begin(), count, m_vertices.begin() + offset);
    }
}
