#include "sampler.h"
#include "device.h"

namespace Comet {
    Sampler::Sampler(Device* device, const SamplerDesc& desc): m_device(device) {
        vk::SamplerCreateInfo sampler_create_info;
        sampler_create_info.magFilter = desc.magFilter;
        sampler_create_info.minFilter = desc.minFilter;
        sampler_create_info.addressModeU = desc.addressModeU;
        sampler_create_info.addressModeV = desc.addressModeV;
        sampler_create_info.addressModeW = desc.addressModeW;
        sampler_create_info.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
        sampler_create_info.maxAnisotropy = desc.maxAnisotropy;
        sampler_create_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;
        sampler_create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler_create_info.mipLodBias = 0.0f;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = VK_LOD_CLAMP_NONE;
        m_sampler = m_device->get().createSampler(sampler_create_info);
    }

    Sampler::~Sampler() {
        m_device->get().destroySampler(m_sampler);
    }

    std::shared_ptr<Sampler> Sampler::create_linear_repeat(Device* device, const float max_anisotropy) {
        SamplerDesc desc{};
        desc.magFilter = vk::Filter::eLinear;
        desc.minFilter = vk::Filter::eLinear;
        desc.addressModeU = vk::SamplerAddressMode::eRepeat;
        desc.addressModeV = vk::SamplerAddressMode::eRepeat;
        desc.addressModeW = vk::SamplerAddressMode::eRepeat;
        desc.maxAnisotropy = max_anisotropy;
        return std::make_shared<Sampler>(device, desc);
    }

    std::shared_ptr<Sampler> Sampler::create_nearest_clamp(Device* device) {
        SamplerDesc desc{};
        desc.magFilter = vk::Filter::eNearest;
        desc.minFilter = vk::Filter::eNearest;
        desc.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        desc.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        desc.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        desc.maxAnisotropy = 1.0f;
        return std::make_shared<Sampler>(device, desc);
    }

    std::shared_ptr<Sampler> Sampler::create_shadow_sampler(Device* device) {
        SamplerDesc desc{};
        desc.magFilter = vk::Filter::eLinear;
        desc.minFilter = vk::Filter::eLinear;
        desc.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        desc.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        desc.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        desc.maxAnisotropy = 1.0f;
        // 边界颜色可在构造中单独处理
        return std::make_shared<Sampler>(device, desc);
    }

    std::shared_ptr<Sampler> SamplerManager::get_linear_repeat(float max_anisotropy) {
        const std::string name = "linear_repeat_" + std::to_string(static_cast<int>(max_anisotropy));
        if (const auto it = m_samplers.find(name); it != m_samplers.end()) {
            return it->second;
        }

        auto sampler = Sampler::create_linear_repeat(m_device, max_anisotropy);
        m_samplers[name] = sampler;
        LOG_INFO("sampler '{}' create and cached successfully", name);
        return sampler;
    }

    std::shared_ptr<Sampler> SamplerManager::get_nearest_clamp() {
        const std::string name = "nearest_clamp";
        if (const auto it = m_samplers.find(name); it != m_samplers.end()) {
            return it->second;
        }

        auto sampler = Sampler::create_nearest_clamp(m_device);
        m_samplers[name] = sampler;
        LOG_INFO("sampler '{}' create and cached successfully", name);
        return sampler;
    }

    std::shared_ptr<Sampler> SamplerManager::get_shadow_sampler() {
        const std::string name = "shadow_sampler";
        if (const auto it = m_samplers.find(name); it != m_samplers.end()) {
            return it->second;
        }

        auto sampler = Sampler::create_shadow_sampler(m_device);
        m_samplers[name] = sampler;
        LOG_INFO("sampler '{}' create and cached successfully", name);
        return sampler;
    }

    std::shared_ptr<Sampler> SamplerManager::create_sampler(const std::string& name, const SamplerDesc& desc) {
        if (const auto it = m_samplers.find(name); it != m_samplers.end()) {
            LOG_DEBUG("sampler {} already exists, skipping create", name);
            return it->second;
        }
        auto sampler = std::make_shared<Sampler>(m_device, desc);
        LOG_INFO("sampler '{}' create and cached successfully", name);
        m_samplers[name] = sampler;
        return sampler;
    }

    std::shared_ptr<Sampler> SamplerManager::get_sampler(const std::string& name) const {
        if (const auto it = m_samplers.find(name); it != m_samplers.end()) {
            return it->second;
        }
        LOG_WARN("no sampler found for name: {}", name);
        return nullptr;
    }

    void SamplerManager::clean_up() {
        m_samplers.clear();
    }
}