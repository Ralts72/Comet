#pragma once

#include "animation/animation_data.h"
#include "scene/entity_uuid.h"

#include <memory>
#include <string_view>

namespace CometEditor {
    // 编辑器拥有预览时钟；只发布姿态输入，不修改 Animator 或推进游戏 Runtime。
    class AnimationPreview {
    public:
        struct Target {
            uint64_t scene_lifetime = 0;
            Comet::EntityUuid entity;
            bool operator==(const Target&) const = default;
        };

        void bind(Target target, std::shared_ptr<const Comet::AnimationData> animation,
            std::string_view initial_clip);
        void reset();
        [[nodiscard]] bool select_clip(std::string_view name);
        [[nodiscard]] bool seek(double time);
        [[nodiscard]] bool play();
        void pause() { m_playing = false; }
        void stop();
        void advance(double delta_time);
        [[nodiscard]] bool set_speed(float speed);
        void set_loop(bool loop) { m_loop = loop; }

        [[nodiscard]] Target target() const { return m_target; }
        [[nodiscard]] const Comet::AnimationData* animation() const { return m_animation.get(); }
        [[nodiscard]] const Comet::AnimationClip* clip() const;
        [[nodiscard]] double time() const { return m_frame.time; }
        [[nodiscard]] bool playing() const { return m_playing; }
        [[nodiscard]] float speed() const { return m_speed; }
        [[nodiscard]] bool loop() const { return m_loop; }
        [[nodiscard]] std::shared_ptr<const Comet::AnimationPlayback> snapshot() const {
            return m_snapshot;
        }

    private:
        void publish();

        Target m_target;
        std::shared_ptr<const Comet::AnimationData> m_animation;
        Comet::AnimationPlayback m_frame;
        std::shared_ptr<const Comet::AnimationPlayback> m_snapshot;
        bool m_playing = false;
        bool m_loop = true;
        float m_speed = 1;
    };
}
