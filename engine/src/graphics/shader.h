#pragma once
#include "vk_common.h"
#include "descriptor_set.h"

#include <cstdint>
#include <span>
namespace Comet {
    class Device;

    struct ShaderLayout {
        std::vector<std::shared_ptr<DescriptorSetLayout>> descriptor_set_layouts;
        std::vector<std::shared_ptr<PushConstantRange>> push_constants;
    };

    class Shader {
    public:
        Shader(Device& device, const std::string& name, std::span<const std::uint32_t> spv_data);

        ~Shader();

        Shader(const Shader&) = delete;

        Shader& operator=(const Shader&) = delete;

        Shader(Shader&&) noexcept = delete;

        Shader& operator=(Shader&&) noexcept = delete;

        [[nodiscard]] vk::ShaderModule get() const { return m_shader_module; }

    private:
        Device& m_device;
        vk::ShaderModule m_shader_module;
    };

    class ShaderManager {
    public:
        explicit ShaderManager(Device& device) : m_device(device) {}

        std::shared_ptr<Shader> load_shader(const std::string& name, std::span<const std::uint32_t> spv_data);

    private:
        Device& m_device;
        std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;
    };
}
