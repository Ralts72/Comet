#pragma once

namespace Comet {
    class AudioCommands;
    class PhysicsCommands;

    // 由组合层装配、运行域借用；服务必须活到 Runtime 停止之后。
    struct RuntimeServices {
        AudioCommands* audio = nullptr;
        PhysicsCommands* physics = nullptr;
    };
}
