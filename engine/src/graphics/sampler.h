#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    struct SamplerDesc {
        vk::Filter magFilter = vk::Filter::eLinear;
        vk::Filter minFilter = vk::Filter::eLinear;
        vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
        vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
        vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
        float maxAnisotropy = 1.0f;
    };

    class Sampler {
    public:
        Sampler(Device* device, const SamplerDesc& desc);
        ~Sampler();

        static std::shared_ptr<Sampler> create_linear_repeat(Device* device, float max_anisotropy = 1.0f);
        static std::shared_ptr<Sampler> create_nearest_clamp(Device* device);
        static std::shared_ptr<Sampler> create_shadow_sampler(Device* device);

        [[nodiscard]] vk::Sampler get() const { return m_sampler; }

    private:
        Device* m_device;
        vk::Sampler m_sampler;
    };

    class SamplerManager {
    public:
        explicit SamplerManager(Device* device): m_device(device) {}
        ~SamplerManager() { clean_up();}

        std::shared_ptr<Sampler> create_sampler(const std::string& name,  const SamplerDesc& desc = {});
        [[nodiscard]] std::shared_ptr<Sampler> get_sampler(const std::string& name) const;

        std::shared_ptr<Sampler> get_linear_repeat(float max_anisotropy = 1.0f);
        std::shared_ptr<Sampler> get_nearest_clamp();
        std::shared_ptr<Sampler> get_shadow_sampler();

        void clean_up();

    private:
        Device* m_device;
        std::unordered_map<std::string, std::shared_ptr<Sampler>> m_samplers;
    };
}