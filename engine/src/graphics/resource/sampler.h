#pragma once
#include "common/export.h"
#include "graphics/result.h"
#include "graphics/enums.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace Comet {
    class Device;

    struct SamplerDesc {
        Filter mag_filter = Filter::Linear;
        Filter min_filter = Filter::Linear;
        SamplerMipmapMode mipmap_mode = SamplerMipmapMode::Linear;
        SamplerAddressMode address_mode_u = SamplerAddressMode::Repeat;
        SamplerAddressMode address_mode_v = SamplerAddressMode::Repeat;
        SamplerAddressMode address_mode_w = SamplerAddressMode::Repeat;
        float max_anisotropy = 1.0f;

        bool operator==(const SamplerDesc&) const = default;
    };

    class COMET_API Sampler {
    public:
        static Result<std::shared_ptr<Sampler>, GraphicsError> create(
            Device& device, const SamplerDesc& desc = {});
        ~Sampler() = default;

        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;
        Sampler(Sampler&&) noexcept = delete;
        Sampler& operator=(Sampler&&) noexcept = delete;

        [[nodiscard]] vk::Sampler get() const { return m_sampler.get(); }
        [[nodiscard]] const SamplerDesc& get_description() const { return m_description; }

    private:
        Sampler(const SamplerDesc& description, vk::UniqueSampler sampler);
        SamplerDesc m_description;
        vk::UniqueSampler m_sampler;
    };

    class COMET_API SamplerManager {
    public:
        explicit SamplerManager(Device& device) : m_device(device) {}

        Result<std::shared_ptr<Sampler>, GraphicsError> create_sampler(
            const std::string& name, const SamplerDesc& desc = {});
        [[nodiscard]] std::shared_ptr<Sampler> get_sampler(const std::string& name) const;

        Result<std::shared_ptr<Sampler>, GraphicsError> get_linear_repeat();
        Result<std::shared_ptr<Sampler>, GraphicsError> get_linear_repeat(float max_anisotropy);
        Result<std::shared_ptr<Sampler>, GraphicsError> get_nearest_clamp();
        Result<std::shared_ptr<Sampler>, GraphicsError> get_shadow_sampler();

    private:
        Device& m_device;
        std::unordered_map<std::string, std::shared_ptr<Sampler>> m_samplers;
    };
}
