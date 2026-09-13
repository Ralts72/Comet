#include "graphics/pipeline/pipeline_key.h"

#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"
#include "graphics/convert.h"

#include <vulkan/vulkan_hash.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace Comet {
    namespace {
        template<typename Value> void hash_value(size_t& seed, const Value& value) {
            seed ^= std::hash<Value>{}(value) + size_t(0x9e3779b9) + (seed << 6) + (seed >> 2);
        }

        template<typename Value> void hash_values(size_t& seed, const std::vector<Value>& values) {
            hash_value(seed, values.size());
            for(const auto& value : values)
                hash_value(seed, value);
        }

        Result<void> canonicalize(PipelineConfig& config) {
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
            std::ranges::sort(attributes, {}, &vk::VertexInputAttributeDescription::location);
            for(size_t index = 1; index < bindings.size(); ++index) {
                if(bindings[index - 1].binding == bindings[index].binding)
                    return Result<void>::failure("Duplicate vertex input binding");
            }
            for(size_t index = 1; index < attributes.size(); ++index) {
                if(attributes[index - 1].location == attributes[index].location)
                    return Result<void>::failure("Duplicate vertex input location");
            }
            const auto& raster = config.rasterization_state;
            const auto& viewport = config.viewport;
            for(const auto value : {raster.line_width, raster.depth_bias_constant_factor,
                    raster.depth_bias_clamp, raster.depth_bias_slope_factor,
                    config.multisample_state.min_sample_shading, viewport.x, viewport.y,
                    viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth}) {
                if(!std::isfinite(value))
                    return Result<void>::failure("Pipeline state contains non-finite values");
            }
            return Result<void>::success();
        }
    }

    Result<PipelineKey> PipelineKey::create(const ShaderLayout& layout, const PipelineConfig& input,
        const Shader& vertex_shader, const Shader& fragment_shader, const RenderPass& pass) {
        PipelineKey candidate;
        candidate.vertex = {
            vertex_shader.get_code(), vertex_shader.get_interface().get_entry_point()};
        candidate.fragment = {
            fragment_shader.get_code(), fragment_shader.get_interface().get_entry_point()};
        candidate.config = input;
        candidate.render_pass = pass.get();
        if(candidate.config.subpass >= pass.get_subpass_count())
            return Result<PipelineKey>::failure("Pipeline subpass is outside render pass");
        if(auto checked = canonicalize(candidate.config); !checked)
            return Result<PipelineKey>::failure(checked.error());
        const auto& vertex = vertex_shader.get_interface();
        if(auto checked = vertex.validate_stage_link(fragment_shader.get_interface()); !checked)
            return Result<PipelineKey>::failure(checked.error());
        const auto& vertex_input = candidate.config.vertex_input_state;
        for(const auto& attribute : vertex_input.vertex_attributes) {
            if(std::ranges::find(vertex_input.vertex_bindings, attribute.binding,
                   &vk::VertexInputBindingDescription::binding)
                == vertex_input.vertex_bindings.end())
                return Result<PipelineKey>::failure(
                    "Vertex attribute location " + std::to_string(attribute.location)
                    + " references missing binding " + std::to_string(attribute.binding));
        }
        for(const auto& input : vertex.get_inputs()) {
            const auto attribute = std::ranges::find(vertex_input.vertex_attributes, input.location,
                &vk::VertexInputAttributeDescription::location);
            const std::string label =
                "Vertex input '" + input.name + "' at location " + std::to_string(input.location);
            if(attribute == vertex_input.vertex_attributes.end())
                return Result<PipelineKey>::failure(label + " has no vertex attribute");
            if(attribute->format != Graphics::format_to_vk(input.format))
                return Result<PipelineKey>::failure(
                    label + " requires an exact 32-bit scalar/vector vertex format");
        }
        if(auto checked = vertex_shader.get_interface().canonicalize_specialization(
               candidate.config.vertex_specialization);
            !checked)
            return Result<PipelineKey>::failure(checked.error());
        if(auto checked = fragment_shader.get_interface().canonicalize_specialization(
               candidate.config.fragment_specialization);
            !checked)
            return Result<PipelineKey>::failure(checked.error());
        for(const auto& set : layout.descriptor_set_layouts) {
            if(!set)
                return Result<PipelineKey>::failure("Pipeline key requires non-null set layouts");
            auto& bindings = candidate.descriptor_sets.emplace_back();
            for(const auto& binding : set->get_bindings()) {
                if(binding.pImmutableSamplers)
                    return Result<PipelineKey>::failure(
                        "Immutable samplers are not supported by pipeline keys");
                bindings.push_back({binding.binding, binding.descriptorType,
                    binding.descriptorCount, binding.stageFlags});
            }
            std::ranges::sort(bindings, {}, &Binding::binding);
            for(size_t index = 1; index < bindings.size(); ++index) {
                if(bindings[index - 1].binding == bindings[index].binding)
                    return Result<PipelineKey>::failure("Duplicate descriptor binding");
            }
        }
        vk::ShaderStageFlags stages;
        for(const auto& range : layout.push_constants) {
            if(!range)
                return Result<PipelineKey>::failure("Pipeline key requires non-null push ranges");
            const auto value = range->get();
            if((stages & value.stageFlags) || !value.stageFlags || value.size == 0
                || value.offset % 4 || value.size % 4) {
                return Result<PipelineKey>::failure(
                    "Invalid or repeated push constant stage range");
            }
            stages |= value.stageFlags;
            candidate.push_constants.push_back(value);
        }
        std::ranges::sort(candidate.push_constants, {}, [](const auto& range) {
            return std::tuple(range.offset, range.size, VkShaderStageFlags(range.stageFlags));
        });
        for(const auto& attachment : pass.get_attachments()) {
            candidate.attachments.push_back(
                {attachment.description.format, attachment.description.samples});
        }
        return Result<PipelineKey>::success(std::move(candidate));
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
        for(const auto* values : {&config.vertex_specialization, &config.fragment_specialization}) {
            hash_value(seed, values->size());
            for(const auto& [id, value] : *values) {
                hash_value(seed, id);
                hash_value(seed, value.get_type());
                hash_value(seed, value.get_bits());
            }
        }
        return seed;
    }
}
