#pragma once
#include "enums.h"
#include <vulkan/vulkan.hpp>
#include <optional>

namespace Comet::Graphics {

    vk::Filter filter_to_vk(Filter filter);

    vk::SamplerAddressMode sampler_address_mode_to_vk(SamplerAddressMode mode);

    vk::CompareOp compare_op_to_vk(CompareOp op);

    vk::ImageType image_type_to_vk(ImageType type);

    vk::SampleCountFlagBits sample_count_to_vk(SampleCount count);

    vk::Format format_to_vk(Format format);

    Format vk_to_format(vk::Format format);

    vk::ImageViewType image_view_type_to_vk(ImageViewType type);

    vk::PrimitiveTopology primitive_topology_to_vk(Topology topology);

    vk::PolygonMode polygon_mode_to_vk(PolygonMode mode);

    vk::FrontFace front_face_to_vk(FrontFace face);

    vk::BlendOp blend_op_to_vk(BlendOp op);

    vk::BlendFactor blend_factor_to_vk(BlendFactor factor);

    vk::StencilOp stencil_op_to_vk(StencilOp op);

    vk::AttachmentLoadOp attachment_load_op_to_vk(AttachmentLoadOp op);

    vk::AttachmentStoreOp attachment_store_op_to_vk(AttachmentStoreOp op);

    vk::IndexType index_type_to_vk(IndexType type);

    vk::Format vertex_format_to_vk(VertexFormat format);

    vk::ShaderStageFlags shader_stage_to_vk(Flags<ShaderStage> flags);

    vk::BorderColor border_color_to_vk(BorderColor color);

    vk::SamplerMipmapMode sampler_mipmap_mode_to_vk(SamplerMipmapMode mode);

    vk::DescriptorType description_type_to_vk(DescriptorType type);

    vk::DescriptorPoolCreateFlags descriptor_pool_create_flags_to_vk(Flags<DescriptorPoolCreateFlag> flags);

    vk::ImageAspectFlags image_aspect_to_vk(Flags<ImageAspect> flags);

    vk::BufferUsageFlags buffer_usage_to_vk(Flags<BufferUsage> flags);

    vk::ImageUsageFlags image_usage_to_vk(Flags<ImageUsage> flags);

    vk::ColorComponentFlags color_write_mask_to_vk(Flags<ColorWriteMask> flags);

    vk::CullModeFlags cull_mode_to_vk(Flags<CullMode> mode);

    vk::PipelineStageFlags pipeline_stage_to_vk(Flags<PipelineStage> flags);

    vk::AccessFlags access_to_vk(Flags<Access> flags);

    vk::DependencyFlags dependency_to_vk(Flags<Dependency> flags);

    vk::ImageLayout image_layout_to_vk(ImageLayout layout);

    vk::PipelineBindPoint pipeline_bind_point_to_vk(PipelineBindPoint point);

    vk::ImageTiling image_tiling_to_vk(ImageTiling tiling);

    vk::SharingMode sharing_mode_to_vk(SharingMode mode);

    vk::ComponentSwizzle component_mapping_to_vk(ComponentMapping swizzle);

    vk::ColorComponentFlags component_mask_to_vk(Flags<ColorComponent> flags);

    vk::ColorSpaceKHR image_color_space_to_vk(ImageColorSpace space);

    ImageColorSpace vk_to_image_color_space(vk::ColorSpaceKHR space);

    vk::MemoryPropertyFlags memory_property_to_vk(Flags<MemoryType> flags);

    vk::VertexInputRate vertex_input_rate_to_vk(VertexInputRate rate);

    vk::DynamicState dynamic_state_to_vk(DynamicState state);

    vk::PresentModeKHR present_mode_to_vk(PresentMode mode);

    PresentMode vk_to_present_mode(vk::PresentModeKHR mode);
}
