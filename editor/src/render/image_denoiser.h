#pragma once

#include "asset/data/texture_data.h"
#include "common/result.h"

#include <memory>

namespace CometEditor {
    // Worker-only filtering. The owner reuses one device/filter set and serializes its jobs.
    class ImageDenoiser {
    public:
        ImageDenoiser();
        ~ImageDenoiser();
        Comet::Result<Comet::TextureData> filter(const Comet::TextureData& color,
            const Comet::TextureData& albedo, const Comet::TextureData& normal);

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
