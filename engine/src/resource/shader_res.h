#pragma once
#include "../pch.h"
#include "refcount.h"

namespace Comet {
    class Device;

    class ShaderRes: public Refcount {
    public:
        ShaderRes(Device &device, const uint32_t *data, size_t size);
        ShaderRes(const ShaderRes&) = delete;
        ShaderRes(ShaderRes&&) = delete;
        ShaderRes& operator=(const ShaderRes&) = delete;
        ShaderRes& operator=(ShaderRes&&) = delete;
        ~ShaderRes();

        void decrease() override;
        [[nodiscard]] vk::ShaderModule getVkShader() const { return m_shader; }
    private:
        vk::ShaderModule m_shader;
        Device& m_device;
    };
}