#pragma once
#include "graphics/vk_common.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/pipeline/shader_interface.h"

#include <cstdint>
#include <span>
#include <map>
namespace Comet {
    class Device;

    struct COMET_API ShaderLayout {
        std::vector<std::shared_ptr<DescriptorSetLayout>> descriptor_set_layouts;
        std::vector<std::shared_ptr<PushConstantRange>> push_constants;

        void validate(const ShaderInterface& shader) const;
    };

    class COMET_API Shader {
    public:
        Shader(Device& device, const std::string& name,
            std::span<const std::uint32_t> spv_data, std::string entry_point = "main");

        ~Shader();

        Shader(const Shader&) = delete;

        Shader& operator=(const Shader&) = delete;

        Shader(Shader&&) noexcept = delete;

        Shader& operator=(Shader&&) noexcept = delete;

        [[nodiscard]] vk::ShaderModule get() const { return m_shader_module; }
        [[nodiscard]] const ShaderInterface& get_interface() const { return m_interface; }
        [[nodiscard]] const std::vector<uint32_t>& get_code() const { return m_code; }

    private:
        Device& m_device;
        ShaderInterface m_interface;
        std::vector<uint32_t> m_code;
        vk::ShaderModule m_shader_module;
    };

    class COMET_API ShaderManager {
    public:
        struct Bytecode {
            std::vector<uint32_t> words;
            std::string entry_point = "main";
        };
        using Bytecodes = std::map<std::string, Bytecode>;
        using Snapshot = std::unordered_map<std::string, std::shared_ptr<Shader>>;
        explicit ShaderManager(Device& device) : m_device(device) {}

        std::shared_ptr<Shader> load_shader(const std::string& name,
            std::span<const std::uint32_t> spv_data, std::string entry_point = "main");
        std::shared_ptr<Shader> load_shader_if_missing(
            const std::string& name, std::span<const uint32_t> words);
        [[nodiscard]] Snapshot prepare_update(const Bytecodes& bytecodes) const;
        void publish_update(Snapshot& candidate) noexcept;
        [[nodiscard]] std::shared_ptr<Shader> get_shader(const std::string& name) const;

    private:
        Device& m_device;
        Snapshot m_shaders;
    };
}
