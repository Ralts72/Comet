#pragma once

namespace Comet {
    class Scene;
    class LineDrawList;
}

namespace CometEditor {
    // Main 在帧捕获前读取呈现姿态，输出自有线段；不访问物理后端。
    void append_collider_outlines(Comet::Scene& scene, Comet::LineDrawList& lines);
    void append_joint_anchors(Comet::Scene& scene, Comet::LineDrawList& lines);
}
