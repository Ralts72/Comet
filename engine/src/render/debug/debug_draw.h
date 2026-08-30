#pragma once

#include "common/export.h"
#include "core/geometry.h"

#include <span>
#include <vector>

namespace Comet {
    struct DebugLineVertex {
        Math::Vec3 position{};
        Math::Vec4 color{1.0f};
    };

    class COMET_API DebugDrawList {
    public:
        [[nodiscard]] bool add_line(
            Math::Vec3 start,
            Math::Vec3 end,
            Math::Vec4 color = Math::Vec4(1.0f));

        [[nodiscard]] bool add_box(
            const AxisAlignedBox& box,
            Math::Vec4 color = Math::Vec4(1.0f));

        void reserve_lines(std::size_t line_count);
        void clear() { m_vertices.clear(); }

        [[nodiscard]] bool empty() const { return m_vertices.empty(); }
        [[nodiscard]] std::size_t line_count() const {
            return m_vertices.size() / 2;
        }
        [[nodiscard]] std::span<const DebugLineVertex> vertices() const {
            return m_vertices;
        }

    private:
        std::vector<DebugLineVertex> m_vertices;
    };
}
