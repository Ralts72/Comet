#pragma once
#include "../common/enums.h"

namespace Comet {
    class SamplerRes;

    class Sampler {
    public:
        struct Descriptor {
            Filter magFilter = Filter::Linear;
            Filter minFilter = Filter::Linear;
            SamplerMipmapMode mipmapMode = SamplerMipmapMode::Linear;
            SamplerAddressMode addressModeU = SamplerAddressMode::Repeat;
            SamplerAddressMode addressModeV = SamplerAddressMode::Repeat;
            SamplerAddressMode addressModeW = SamplerAddressMode::Repeat;
            float mipLodBias = 0.0;
            bool anisotropyEnable = false;
            float maxAnisotropy = 0.0;
            bool compareEnable = false;
            CompareOp compareOp = CompareOp::Always;
            float minLod = 0;
            float maxLod = 0;
            BorderColor borderColor = BorderColor::IntOpaqueWhite;
            bool unnormalizedCoordinates = false;
        };

        Sampler() = default;

        explicit Sampler(SamplerRes*);

        Sampler(const Sampler&);

        Sampler(Sampler&&) noexcept;

        Sampler& operator=(const Sampler&) noexcept;

        Sampler& operator=(Sampler&&) noexcept;

        ~Sampler();

        const SamplerRes& getRes() const noexcept { return *m_res; }

        SamplerRes& getRes() noexcept { return *m_res; }

        operator bool() const noexcept;

        void release();

    private:
        SamplerRes* m_res{};
    };
}
