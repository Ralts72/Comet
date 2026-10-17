#pragma once

#include "animation/animation_data.h"
#include "asset/handle.h"
#include "asset/data/mesh_lod.h"

namespace Comet {
    struct ModelData {
        static constexpr uint32_t NO_PARENT = std::numeric_limits<uint32_t>::max();
        struct Part {
            AssetHandle mesh;
            AssetHandle material;
            MeshLod lod;
            bool collision = false;
        };
        struct Node {
            std::string key;
            std::string name;
            uint32_t parent = NO_PARENT;
            JointTransform transform;
            // XYZ 根部摆角（度），W 频率（Hz）；零摆角关闭。
            Math::Vec4 wind{0, 0, 0, 1};
            std::vector<Part> parts;
        };
        struct Scene {
            std::string name;
            std::vector<uint32_t> roots;
        };
        std::vector<Node> nodes;
        std::vector<Scene> scenes;
        uint32_t default_scene = 0;
    };
}
