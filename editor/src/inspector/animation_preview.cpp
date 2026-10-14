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
            m_frame.current.clip = initial_clip;
        else
            m_frame.current.clip = m_animation->clips.front().name;
    }

    void AnimationPreview::reset() {
        m_target = {};
        m_animation.reset();
        m_frame = {.current = {.loop = false}};
        m_snapshot.reset();
        m_playing = false;
        m_loop = true;
        m_speed = 1;
    }

    const Comet::AnimationClip* AnimationPreview::clip() const {
        return m_animation ? m_animation->find_clip(m_frame.current.clip) : nullptr;
    }

    bool AnimationPreview::select_clip(std::string_view name) {
        if(!m_animation || !m_animation->find_clip(name))
            return false;
        m_frame.current.clip = name;
        return seek(0);
    }

    bool AnimationPreview::select_sample(Comet::AnimationBlend sample, bool restart) {
        if(!m_animation || !Comet::animation_duration(*m_animation, sample))
            return false;
        if(!restart && sample.normalized_time == m_frame.current.normalized_time)
            sample.time = m_frame.current.time;
        if(sample == m_frame.current)
            return true;
        m_frame.current = std::move(sample);
        m_loop = m_frame.current.loop;
        publish();
        return true;
    }

    double AnimationPreview::duration() const {
        if(!m_animation)
            return 0;
        const auto duration = Comet::animation_duration(*m_animation, m_frame.current);
        if(!duration)
            return 0;
        return m_frame.current.normalized_time ? 1 : duration.value();
    }

    bool AnimationPreview::seek(double time) {
        const auto* selected = clip();
        if(!selected || !std::isfinite(time))
            return false;
        m_frame.current.time = std::clamp(time, 0.0, duration());
        pause();
        publish();
        return true;
    }

    bool AnimationPreview::play() {
        const auto* selected = clip();
        if(!selected || duration() <= 0)
            return false;
        if(m_frame.current.time >= duration())
            m_frame.current.time = 0;
        m_playing = true;
        publish();
        return true;
    }

    void AnimationPreview::stop() {
        pause();
        m_frame.current.time = 0;
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
        double step = delta_time * m_speed;
        if(m_frame.current.normalized_time) {
            const auto period = Comet::animation_duration(*m_animation, m_frame.current);
            if(!period || period.value() <= 0)
                return;
            step /= period.value();
        }
        const double next = m_frame.current.time + step;
        if(!std::isfinite(next))
            return;
        m_frame.current.time = Comet::animation_sample_time(next, duration(), m_loop);
        if(!m_loop && m_frame.current.time == duration())
            pause();
        publish();
    }

    void AnimationPreview::publish() {
        if(!m_snapshot || *m_snapshot != m_frame)
            m_snapshot = std::make_shared<const Comet::AnimationPlayback>(m_frame);
    }
}
