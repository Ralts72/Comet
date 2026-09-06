#pragma once

#include "common/export.h"
#include "core/geometry.h"

#include <span>
#include <vector>

namespace Comet {
    // 通用世界空间线段绘制数据，不保存相机、实体或 GPU 资源。
    class COMET_API LineDrawList {
    public:
        struct Vertex {
            Math::Vec3 position{};
            Math::Vec4 color{1.0f};
        };

        [[nodiscard]] bool add_line(
            Math::Vec3 start, Math::Vec3 end, Math::Vec4 color = Math::Vec4(1.0f));
        [[nodiscard]] bool add_box(
            const BoundingBox& box, Math::Vec4 color = Math::Vec4(1.0f));
        // 变换八个角点后连边，保留旋转/缩放，不重新拟合世界轴对齐盒。
        [[nodiscard]] bool add_box(const BoundingBox& box, const Math::Mat4& transform,
            Math::Vec4 color = Math::Vec4(1.0f));

        void append(const LineDrawList& draw_list);
        void clear() { m_vertices.clear(); }

        [[nodiscard]] bool empty() const { return m_vertices.empty(); }
        [[nodiscard]] std::size_t line_count() const { return m_vertices.size() / 2; }
        [[nodiscard]] std::span<const Vertex> vertices() const { return m_vertices; }

    private:
        std::vector<Vertex> m_vertices;
    };
}
