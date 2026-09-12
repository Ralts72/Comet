#pragma once

#include "common/export.h"
#include "graphics/pipeline/pipeline_config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Comet {
    class Shader;
    struct ShaderLayout;
    class RenderPass;

    // Device/RenderPass-local object key, not a persistent cache format.
    struct COMET_API PipelineKey {
        struct ShaderCode {
            std::vector<uint32_t> words;
            std::string entry_point;
            bool operator==(const ShaderCode&) const = default;
        };
        struct Binding {
            uint32_t binding;
            vk::DescriptorType type;
            uint32_t count;
            vk::ShaderStageFlags stages;
            bool operator==(const Binding&) const = default;
        };
        struct AttachmentFormat {
            Format format;
            SampleCount samples;
            bool operator==(const AttachmentFormat&) const = default;
        };
        struct Hash {
            size_t operator()(const PipelineKey& key) const;
        };

        ShaderCode vertex;
        ShaderCode fragment;
        std::vector<std::vector<Binding>> descriptor_sets;
        std::vector<vk::PushConstantRange> push_constants;
        PipelineConfig config;
        vk::RenderPass render_pass;
        std::vector<AttachmentFormat> attachments;

        PipelineKey(const ShaderLayout& layout, const PipelineConfig& config,
            const Shader& vertex, const Shader& fragment, const RenderPass& render_pass);
        bool operator==(const PipelineKey&) const = default;
    };
}
