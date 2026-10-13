#include "inspector/animation_preview.h"

#include <algorithm>
#include <cmath>

namespace CometEditor {
    void AnimationPreview::bind(Target target,
        std::shared_ptr<const Comet::AnimationData> animation, std::string_view initial_clip) {
        if(m_target == target && m_animation == animation)
            return;
        reset();
        m_target = target;
        m_animation = std::move(animation);
        if(!m_animation || m_animation->clips.empty())
            return;
        if(m_animation->find_clip(initial_clip))
            m_frame.clip = initial_clip;
        else
            m_frame.clip = m_animation->clips.front().name;
    }

    void AnimationPreview::reset() {
        m_target = {};
        m_animation.reset();
        m_frame = {.loop = false};
        m_snapshot.reset();
        m_playing = false;
        m_loop = true;
        m_speed = 1;
    }

    const Comet::AnimationClip* AnimationPreview::clip() const {
        return m_animation ? m_animation->find_clip(m_frame.clip) : nullptr;
    }

    bool AnimationPreview::select_clip(std::string_view name) {
        if(!m_animation || !m_animation->find_clip(name))
            return false;
        m_frame.clip = name;
        return seek(0);
    }

    bool AnimationPreview::seek(double time) {
        const auto* selected = clip();
        if(!selected || !std::isfinite(time))
            return false;
        m_frame.time = std::clamp(time, 0.0, double(selected->duration));
        pause();
        publish();
        return true;
    }

    bool AnimationPreview::play() {
        const auto* selected = clip();
        if(!selected || selected->duration <= 0)
            return false;
        if(m_frame.time >= selected->duration)
            m_frame.time = 0;
        m_playing = true;
        publish();
        return true;
    }

    void AnimationPreview::stop() {
        pause();
        m_frame.time = 0;
        m_snapshot.reset();
    }

    bool AnimationPreview::set_speed(float speed) {
        if(!std::isfinite(speed) || speed < 0 || speed > 4)
            return false;
        m_speed = speed;
        return true;
    }

    void AnimationPreview::advance(double delta_time) {
        const auto* selected = clip();
        if(!m_playing || !selected || !std::isfinite(delta_time) || delta_time <= 0)
            return;
        const double next = m_frame.time + delta_time * m_speed;
        if(!std::isfinite(next))
            return;
        if(m_loop)
            m_frame.time = std::fmod(next, double(selected->duration));
        else {
            m_frame.time = std::min(next, double(selected->duration));
            if(m_frame.time == selected->duration)
                pause();
        }
        publish();
    }

    void AnimationPreview::publish() {
        if(!m_snapshot || *m_snapshot != m_frame)
            m_snapshot = std::make_shared<const Comet::AnimationPlayback>(m_frame);
    }
}
