#include "graphics/resource/sampler.h"
#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "diagnostics/logger.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace Comet {
    Sampler::Sampler(const SamplerDesc& description, vk::UniqueSampler sampler)
        : m_description(description), m_sampler(std::move(sampler)) {}

    Result<std::shared_ptr<Sampler>, GraphicsError> Sampler::create(
        Device& device, const SamplerDesc& desc) {
        const auto valid_filter = [](Filter filter) {
            return filter == Filter::Nearest || filter == Filter::Linear;
        };
        const auto valid_address = [](SamplerAddressMode mode) {
            switch(mode) {
                case SamplerAddressMode::ClampToEdge:
                case SamplerAddressMode::Repeat:
                case SamplerAddressMode::MirrorRepeat:
                case SamplerAddressMode::ClampToBorder:
                    return true;
            }
            return false;
        };
        if(!valid_filter(desc.mag_filter) || !valid_filter(desc.min_filter)
            || !valid_address(desc.address_mode_u) || !valid_address(desc.address_mode_v)
            || !valid_address(desc.address_mode_w))
            return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(
                {"Unsupported sampler filter or address mode"});
        if(!std::isfinite(desc.max_anisotropy) || desc.max_anisotropy < 1.0f)
            return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(
                {"Sampler max anisotropy must be finite and at least 1.0"});
        const float enabled_max_anisotropy = device.get_capability().max_sampler_anisotropy;
        if(desc.max_anisotropy > enabled_max_anisotropy)
            return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(
                {"Sampler anisotropy exceeds the enabled device maximum "
                    + std::to_string(enabled_max_anisotropy)});

        vk::SamplerCreateInfo info;
        info.magFilter = Graphics::filter_to_vk(desc.mag_filter);
        info.minFilter = Graphics::filter_to_vk(desc.min_filter);
        info.addressModeU = Graphics::sampler_address_mode_to_vk(desc.address_mode_u);
        info.addressModeV = Graphics::sampler_address_mode_to_vk(desc.address_mode_v);
        info.addressModeW = Graphics::sampler_address_mode_to_vk(desc.address_mode_w);
        info.anisotropyEnable = desc.max_anisotropy > 1.0f;
        info.maxAnisotropy = desc.max_anisotropy;
        info.borderColor = vk::BorderColor::eIntOpaqueBlack;
        info.unnormalizedCoordinates = false;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.mipLodBias = 0.0f;
        info.minLod = 0.0f;
        info.maxLod = VK_LOD_CLAMP_NONE;
        auto handle = Graphics::create_handle<vk::Sampler>(
            device.get(), "Create sampler", [&](vk::Sampler* output) noexcept {
                return device.get().createSampler(&info, nullptr, output);
            });
        if(!handle)
            return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(handle.error());
        return Result<std::shared_ptr<Sampler>, GraphicsError>::success(
            std::shared_ptr<Sampler>(new Sampler(desc, std::move(handle).value())));
    }

    Result<std::shared_ptr<Sampler>, GraphicsError> SamplerManager::get_linear_repeat() {
        return get_linear_repeat(m_device.get_capability().max_sampler_anisotropy);
    }

    Result<std::shared_ptr<Sampler>, GraphicsError> SamplerManager::get_linear_repeat(
        const float max_anisotropy) {
        const std::string name =
            "linear_repeat_" + std::to_string(std::bit_cast<uint32_t>(max_anisotropy));
        SamplerDesc desc;
        desc.max_anisotropy = max_anisotropy;
        return create_sampler(name, desc);
    }

    Result<std::shared_ptr<Sampler>, GraphicsError> SamplerManager::get_nearest_clamp() {
        SamplerDesc desc;
        desc.mag_filter = Filter::Nearest;
        desc.min_filter = Filter::Nearest;
        desc.address_mode_u = SamplerAddressMode::ClampToEdge;
        desc.address_mode_v = SamplerAddressMode::ClampToEdge;
        desc.address_mode_w = SamplerAddressMode::ClampToEdge;
        return create_sampler("nearest_clamp", desc);
    }

    Result<std::shared_ptr<Sampler>, GraphicsError> SamplerManager::get_shadow_sampler() {
        SamplerDesc desc;
        desc.address_mode_u = SamplerAddressMode::ClampToBorder;
        desc.address_mode_v = SamplerAddressMode::ClampToBorder;
        desc.address_mode_w = SamplerAddressMode::ClampToBorder;
        return create_sampler("shadow_sampler", desc);
    }

    Result<std::shared_ptr<Sampler>, GraphicsError> SamplerManager::create_sampler(
        const std::string& name, const SamplerDesc& desc) {
        if(const auto it = m_samplers.find(name); it != m_samplers.end()) {
            if(it->second->get_description() != desc)
                return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(
                    {"Sampler '" + name + "' already exists with a different description"});
            return Result<std::shared_ptr<Sampler>, GraphicsError>::success(it->second);
        }
        auto sampler = Sampler::create(m_device, desc);
        if(!sampler)
            return Result<std::shared_ptr<Sampler>, GraphicsError>::failure(
                {"Sampler '" + name + "': " + sampler.error().message, sampler.error().result});
        m_samplers.emplace(name, sampler.value());
        LOG_INFO("Sampler '{}' created and cached successfully", name);
        return sampler;
    }

    std::shared_ptr<Sampler> SamplerManager::get_sampler(const std::string& name) const {
        if(const auto it = m_samplers.find(name); it != m_samplers.end())
            return it->second;
        LOG_WARN("No sampler found for name: {}", name);
        return nullptr;
    }
}
