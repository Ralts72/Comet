#include "graphics/pipeline/pipeline_key.h"

#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"

#include <vulkan/vulkan_hash.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace Comet {
    namespace {
        template<typename Value> void hash_value(size_t& seed, const Value& value) {
            seed ^= std::hash<Value>{}(value) + size_t(0x9e3779b9) + (seed << 6)
                    + (seed >> 2);
        }

        template<typename Value>
        void hash_values(size_t& seed, const std::vector<Value>& values) {
            hash_value(seed, values.size());
            for(const auto& value : values)
                hash_value(seed, value);
        }

        void canonicalize(PipelineConfig& config) {
            auto& dynamic = config.dynamic_state.dynamic_states;
            std::ranges::sort(dynamic);
            dynamic.erase(std::unique(dynamic.begin(), dynamic.end()), dynamic.end());
            if(std::ranges::find(dynamic, vk::DynamicState::eViewport) != dynamic.end())
                config.viewport = PipelineConfig{}.viewport;
            if(std::ranges::find(dynamic, vk::DynamicState::eScissor) != dynamic.end())
                config.scissor = PipelineConfig{}.scissor;
            auto& bindings = config.vertex_input_state.vertex_bindings;
            auto& attributes = config.vertex_input_state.vertex_attributes;
            std::ranges::sort(bindings, {}, &vk::VertexInputBindingDescription::binding);
            std::ranges::sort(
                attributes, {}, &vk::VertexInputAttributeDescription::location);
            for(size_t index = 1; index < bindings.size(); ++index) {
                if(bindings[index - 1].binding == bindings[index].binding)
                    throw std::invalid_argument("Duplicate vertex input binding");
            }
            for(size_t index = 1; index < attributes.size(); ++index) {
                if(attributes[index - 1].location == attributes[index].location)
                    throw std::invalid_argument("Duplicate vertex input location");
            }
            const auto& raster = config.rasterization_state;
            const auto& viewport = config.viewport;
            for(const auto value : {raster.line_width, raster.depth_bias_constant_factor,
                    raster.depth_bias_clamp, raster.depth_bias_slope_factor,
                    config.multisample_state.min_sample_shading, viewport.x, viewport.y,
                    viewport.width, viewport.height, viewport.minDepth,
                    viewport.maxDepth}) {
                if(!std::isfinite(value))
                    throw std::invalid_argument(
                        "Pipeline state contains non-finite values");
            }
        }
    }

    PipelineKey::PipelineKey(const ShaderLayout& layout, const PipelineConfig& input,
        const Shader& vertex_shader, const Shader& fragment_shader,
        const RenderPass& pass)
        : vertex{
              vertex_shader.get_code(), vertex_shader.get_interface().get_entry_point()},
          fragment{fragment_shader.get_code(),
              fragment_shader.get_interface().get_entry_point()},
          config(input), render_pass(pass.get()) {
        if(config.subpass >= pass.get_subpass_count())
            throw std::invalid_argument("Pipeline subpass is outside render pass");
        canonicalize(config);
        for(const auto& set : layout.descriptor_set_layouts) {
            if(!set)
                throw std::invalid_argument("Pipeline key requires non-null set layouts");
            auto& bindings = descriptor_sets.emplace_back();
            for(const auto& binding : set->get_bindings()) {
                if(binding.pImmutableSamplers)
                    throw std::invalid_argument(
                        "Immutable samplers are not supported by pipeline keys");
                bindings.push_back({binding.binding, binding.descriptorType,
                    binding.descriptorCount, binding.stageFlags});
            }
            std::ranges::sort(bindings, {}, &Binding::binding);
            for(size_t index = 1; index < bindings.size(); ++index) {
                if(bindings[index - 1].binding == bindings[index].binding)
                    throw std::invalid_argument("Duplicate descriptor binding");
            }
        }
        vk::ShaderStageFlags stages;
        for(const auto& range : layout.push_constants) {
            if(!range)
                throw std::invalid_argument("Pipeline key requires non-null push ranges");
            const auto value = range->get();
            if((stages & value.stageFlags) || !value.stageFlags || value.size == 0
                || value.offset % 4 || value.size % 4) {
                throw std::invalid_argument(
                    "Invalid or repeated push constant stage range");
            }
            stages |= value.stageFlags;
            push_constants.push_back(value);
        }
        std::ranges::sort(push_constants, {}, [](const auto& range) {
            return std::tuple(
                range.offset, range.size, VkShaderStageFlags(range.stageFlags));
        });
        for(const auto& attachment : pass.get_attachments()) {
            attachments.push_back(
                {attachment.description.format, attachment.description.samples});
        }
    }

    size_t PipelineKey::Hash::operator()(const PipelineKey& key) const {
        size_t seed = 0;
        hash_values(seed, key.vertex.words);
        hash_value(seed, key.vertex.entry_point);
        hash_values(seed, key.fragment.words);
        hash_value(seed, key.fragment.entry_point);
        hash_value(seed, key.descriptor_sets.size());
        for(const auto& set : key.descriptor_sets) {
            hash_value(seed, set.size());
            for(const auto& binding : set) {
                hash_value(seed, binding.binding);
                hash_value(seed, binding.type);
                hash_value(seed, binding.count);
                hash_value(seed, VkShaderStageFlags(binding.stages));
            }
        }
        hash_values(seed, key.push_constants);
        hash_value(seed, key.render_pass);
        hash_value(seed, key.attachments.size());
        for(const auto& attachment : key.attachments) {
            hash_value(seed, attachment.format);
            hash_value(seed, attachment.samples);
        }
        const auto& config = key.config;
        hash_values(seed, config.vertex_input_state.vertex_bindings);
        hash_values(seed, config.vertex_input_state.vertex_attributes);
        hash_value(seed, config.input_assembly_state.topology);
        hash_value(seed, config.input_assembly_state.primitive_restart_enable);
        const auto& raster = config.rasterization_state;
        hash_value(seed, raster.depth_clamp_enable);
        hash_value(seed, raster.rasterizer_discard_enable);
        hash_value(seed, raster.polygon_mode);
        hash_value(seed, raster.cull_mode);
        hash_value(seed, raster.front_face);
        hash_value(seed, raster.depth_bias_enable);
        hash_value(seed, raster.depth_bias_constant_factor);
        hash_value(seed, raster.depth_bias_clamp);
        hash_value(seed, raster.depth_bias_slope_factor);
        hash_value(seed, raster.line_width);
        hash_value(seed, config.multisample_state.rasterization_samples);
        hash_value(seed, config.multisample_state.sample_shading_enable);
        hash_value(seed, config.multisample_state.min_sample_shading);
        const auto& depth = config.depth_stencil_state;
        hash_value(seed, depth.depth_test_enable);
        hash_value(seed, depth.depth_write_enable);
        hash_value(seed, depth.depth_compare_op);
        hash_value(seed, depth.depth_bounds_test_enable);
        hash_value(seed, depth.stencil_test_enable);
        hash_value(seed, config.viewport);
        hash_value(seed, config.scissor);
        hash_value(seed, config.color_blend_state);
        hash_values(seed, config.dynamic_state.dynamic_states);
        hash_value(seed, config.subpass);
        return seed;
    }
}
