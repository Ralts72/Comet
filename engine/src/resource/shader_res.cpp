#include "shader_res.h"
#include "../common/macro.h"
#include "../graphics/device.h"

namespace Comet {
    ShaderRes::ShaderRes(Device &device, const uint32_t *data, size_t size): m_device(device){
        ASSERT(size % 4 == 0 && data, "invalid SPIR-V data");
        vk::ShaderModuleCreateInfo ci{};
        ci.pCode = data;
        ci.codeSize = size;
        m_shader = m_device.getVkDevice().createShaderModule(ci);
    }

    ShaderRes::~ShaderRes(){
        m_device.getVkDevice().destroyShaderModule(m_shader);
    }

    void ShaderRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            //TODO
            // m_device.m_pending_delete_shader_modules.push_back(this);
        }
    }
}