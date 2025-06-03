#include "sampler_res.h"
#include "../common/enum_convert.h"
#include "graphics/device.h"

namespace Comet {
    SamplerRes::SamplerRes(Device& device, const Sampler::Descriptor& desc): m_device(device) {
        vk::SamplerCreateInfo ci{};
        ci.magFilter = filter2VK(desc.magFilter);
        ci.minFilter = filter2VK(desc.minFilter);
        ci.mipmapMode = samplerMipmapMode2VK(desc.mipmapMode);
        ci.addressModeU = samplerAddressMode2VK(desc.addressModeU);
        ci.addressModeV = samplerAddressMode2VK(desc.addressModeV);
        ci.addressModeW = samplerAddressMode2VK(desc.addressModeW);
        ci.mipLodBias = desc.mipLodBias;
        ci.anisotropyEnable = desc.anisotropyEnable;
        ci.maxAnisotropy = desc.maxAnisotropy;
        ci.compareEnable = desc.compareEnable;
        ci.compareOp = compareOp2VK(desc.compareOp);
        ci.minLod = desc.minLod;
        ci.maxLod = desc.maxLod;
        ci.borderColor = borderColor2VK(desc.borderColor);
        ci.unnormalizedCoordinates = desc.unnormalizedCoordinates;

        m_sampler = m_device.getVkDevice().createSampler(ci);
    }

    SamplerRes::~SamplerRes() {
        m_device.getVkDevice().destroySampler(m_sampler);
    }

    void SamplerRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            //TODO
        }
    }
}
