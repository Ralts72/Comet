#pragma once
#include "../pch.h"
#include "refcount.h"
#include "../graphics/sampler.h"

namespace Comet {
    class Device;

    class SamplerRes final: public Refcount {
    public:
        explicit SamplerRes(Device& device, const Sampler::Descriptor&);

        SamplerRes(const SamplerRes&) = delete;

        SamplerRes(SamplerRes&&) = delete;

        SamplerRes& operator=(const SamplerRes&) = delete;

        SamplerRes& operator=(SamplerRes&&) = delete;

        ~SamplerRes() override;

        void decrease() override;

        [[nodiscard]] vk::Sampler getSampler() const { return m_sampler; }

    private:
        vk::Sampler m_sampler;
        Device& m_device;
    };
}
