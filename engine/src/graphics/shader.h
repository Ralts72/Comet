#pragma once
#include "pch.h"
#include "vk_common.h"

namespace Comet {
    class Device;

    struct ShaderLayout{
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
        std::vector<vk::PushConstantRange> push_constants;
    };

    class Shader {
    public:
        Shader(Device* device, const std::string& name, const std::vector<unsigned char>& spv_data, const ShaderLayout& layout = {});
        ~Shader();

        [[nodiscard]] vk::ShaderModule get() const { return m_shader_module; }
        [[nodiscard]] ShaderLayout get_layout() const { return m_layout; }
        [[nodiscard]] const std::string& get_name() const { return m_name; }

    private:
        Device* m_device;
        vk::ShaderModule m_shader_module;
        std::string m_name;
        std::vector<unsigned char> m_spv_data;
        ShaderLayout m_layout;
    };

    class ShaderManager {
    public:
        explicit ShaderManager(Device* device): m_device(device) {}
        ~ShaderManager() { clean_up();}

        std::shared_ptr<Shader> load_shader(const std::string& name, const std::vector<unsigned char>& spv_data, const ShaderLayout& layout = {});
        [[nodiscard]] std::shared_ptr<Shader> get_shader(const std::string& name) const;
        void clean_up();

    private:
        Device* m_device;
        std::unordered_map<std::string, std::shared_ptr<Shader>> m_shaders;
    };
}