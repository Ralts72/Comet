#pragma once

#include "animation/animation_data.h"
#include "asset/handle.h"

namespace Comet {
    struct ModelData {
        static constexpr uint32_t NO_PARENT = std::numeric_limits<uint32_t>::max();
        struct Part {
            AssetHandle mesh;
            AssetHandle material;
        };
        struct Node {
            std::string key;
            std::string name;
            uint32_t parent = NO_PARENT;
            JointTransform transform;
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
