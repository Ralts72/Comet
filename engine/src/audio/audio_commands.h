#pragma once

#include "asset/handle.h"
#include "audio/audio_category.h"
#include "common/export.h"

namespace Comet {
    class Scene;
    class SceneRuntime;

    // 运行代码只提交声音快照，不接触设备或播放实例。
    class COMET_API AudioCommands {
    public:
        virtual ~AudioCommands() = default;
        [[nodiscard]] virtual bool is_bound_to(const Scene& scene) const noexcept = 0;
        [[nodiscard]] virtual bool request_one_shot(
            AssetHandle clip, float volume, AudioCategory category = AudioCategory::Effects) = 0;

    private:
        friend class SceneRuntime;
        virtual bool begin(const Scene& scene, bool paused) = 0;
        virtual void set_paused(bool paused) noexcept = 0;
        virtual void end() noexcept = 0;
    };
}
