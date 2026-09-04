#include "render_pass.h"
#include "device.h"

#include <algorithm>

namespace Comet {
    RenderPass::RenderPass(Device& device, const std::vector<Attachment>& attachments,
        const std::vector<RenderSubPass>& sub_passes, const Format surface_format)
        : m_device(device), m_attachments(attachments) {
        std::vector<RenderSubPass> actual_sub_passes = sub_passes;
        // 1. default subpass and attachment
        if(sub_passes.empty() && attachments.empty()) {
            Attachment::Description description{};
            description.format = surface_format;
            description.load_op = AttachmentLoadOp::Clear;
            description.store_op = AttachmentStoreOp::Store;
            description.stencil_load_op = AttachmentLoadOp::DontCare;
            description.stencil_store_op = AttachmentStoreOp::DontCare;
            description.initial_layout = ImageLayout::Undefined;
            description.final_layout = ImageLayout::PresentSrcKHR;
            Attachment attachment = {.description = description,
                .usage = Flags<ImageUsage>(ImageUsage::ColorAttachment)};
            SubpassColorAttachment subpass_attachment(0);
            const RenderSubPass render_sub_pass = {
                .color_attachments = {subpass_attachment},
                .sample_count = SampleCount::Count1};
            m_attachments.push_back(attachment);
            actual_sub_passes.push_back(render_sub_pass);
        }
        // 2.subpass
        // index check
        for(const auto& sub_pass : actual_sub_passes) {
            for(const auto& attachment : sub_pass.input_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("input attachment index exceeds attachment pool ");
                }
            }
            for(const auto& attachment : sub_pass.color_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("color attachment index exceeds attachment pool ");
                }
            }
            for(const auto& attachment : sub_pass.depth_stencil_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("depth stencil attachment index exceeds attachment pool ");
                }
            }
        }
        std::vector<vk::SubpassDescription> sub_pass_descriptions(
            actual_sub_passes.size());
        std::vector<vk::AttachmentReference> resolve_attachments_reference(
            actual_sub_passes.size());

        std::vector<std::vector<vk::AttachmentReference>> all_input_attachments_reference(
            actual_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>> all_color_attachments_reference(
            actual_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>>
            all_depth_stencil_attachments_reference(actual_sub_passes.size());

        for(uint32_t i = 0; i < actual_sub_passes.size(); ++i) {
            const RenderSubPass& sub_pass = actual_sub_passes[i];
            const auto& input_attachments = sub_pass.input_attachments;
            const auto& color_attachments = sub_pass.color_attachments;
            const auto& depth_stencil_attachments = sub_pass.depth_stencil_attachments;
            const SampleCount sample_count = sub_pass.sample_count;

            for(const auto& attachment : input_attachments) {
                vk::AttachmentReference reference = {
                    attachment.index, Graphics::image_layout_to_vk(attachment.layout)};
                all_input_attachments_reference[i].emplace_back(reference);
            }

            for(const auto& attachment : color_attachments) {
                vk::AttachmentReference reference = {
                    attachment.index, Graphics::image_layout_to_vk(attachment.layout)};
                all_color_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].description.samples = sample_count;
                if(sample_count > SampleCount::Count1) {
                    m_attachments[attachment.index].description.final_layout =
                        attachment.layout;
                }
            }

            for(const auto& attachment : depth_stencil_attachments) {
                vk::AttachmentReference reference = {
                    attachment.index, Graphics::image_layout_to_vk(attachment.layout)};
                all_depth_stencil_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].description.samples = sample_count;
                m_attachments[attachment.index].description.final_layout =
                    attachment.layout;
            }

            if(sample_count > SampleCount::Count1) {
                Attachment::Description msaa_description{};
                msaa_description.format = surface_format;
                msaa_description.samples = SampleCount::Count1;
                msaa_description.load_op = AttachmentLoadOp::DontCare;
                msaa_description.store_op = AttachmentStoreOp::Store;
                msaa_description.stencil_load_op = AttachmentLoadOp::DontCare;
                msaa_description.stencil_store_op = AttachmentStoreOp::DontCare;
                msaa_description.initial_layout = ImageLayout::Undefined;
                msaa_description.final_layout = sub_pass.resolve_final_layout;

                Attachment msaa_attachment = {
                    .description = msaa_description, .usage = sub_pass.resolve_usage};

                m_attachments.push_back(msaa_attachment);
                vk::AttachmentReference reference = {
                    static_cast<uint32_t>(m_attachments.size() - 1),
                    vk::ImageLayout::eColorAttachmentOptimal};
                resolve_attachments_reference[i] = reference;
            }

            sub_pass_descriptions[i].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            sub_pass_descriptions[i].inputAttachmentCount =
                all_input_attachments_reference[i].size();
            sub_pass_descriptions[i].pInputAttachments =
                all_input_attachments_reference[i].data();
            sub_pass_descriptions[i].colorAttachmentCount =
                all_color_attachments_reference[i].size();
            sub_pass_descriptions[i].pColorAttachments =
                all_color_attachments_reference[i].data();
            sub_pass_descriptions[i].pResolveAttachments =
                (sample_count > SampleCount::Count1 ? &resolve_attachments_reference[i]
                                                    : nullptr);
            sub_pass_descriptions[i].pDepthStencilAttachment =
                all_depth_stencil_attachments_reference[i].data();
            sub_pass_descriptions[i].preserveAttachmentCount = 0;
            sub_pass_descriptions[i].pPreserveAttachments = nullptr;
        }
        std::vector<vk::SubpassDependency> dependencies;
        if(actual_sub_passes.size() > 1) {
            dependencies.reserve(actual_sub_passes.size());
            for(uint32_t j = 0; j + 1 < actual_sub_passes.size(); ++j) {
                vk::SubpassDependency dependency{};
                dependency.srcSubpass = j;
                dependency.dstSubpass = j + 1;
                dependency.srcStageMask =
                    vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
                dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                dependency.dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;
                dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;
                dependencies.push_back(dependency);
            }
        }

        const bool has_sampled_output =
            std::ranges::any_of(m_attachments, [](const Attachment& attachment) {
                return attachment.description.final_layout
                       == ImageLayout::ShaderReadOnlyOptimal;
            });
        if(has_sampled_output) {
            vk::SubpassDependency dependency{};
            dependency.srcSubpass = static_cast<uint32_t>(actual_sub_passes.size() - 1);
            dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;
            dependencies.push_back(dependency);
        }
        // 3. create info
        std::vector<vk::AttachmentDescription> attachment_descriptions;
        attachment_descriptions.reserve(m_attachments.size());
        for(const auto& [description, usage] : m_attachments) {
            vk::AttachmentDescription vk_description{};
            vk_description.format = Graphics::format_to_vk(description.format);
            vk_description.samples = Graphics::sample_count_to_vk(description.samples);
            vk_description.loadOp =
                Graphics::attachment_load_op_to_vk(description.load_op);
            vk_description.storeOp =
                Graphics::attachment_store_op_to_vk(description.store_op);
            vk_description.stencilLoadOp =
                Graphics::attachment_load_op_to_vk(description.stencil_load_op);
            vk_description.stencilStoreOp =
                Graphics::attachment_store_op_to_vk(description.stencil_store_op);
            vk_description.initialLayout =
                Graphics::image_layout_to_vk(description.initial_layout);
            vk_description.finalLayout =
                Graphics::image_layout_to_vk(description.final_layout);
            attachment_descriptions.push_back(vk_description);
        }
        vk::RenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.attachmentCount =
            static_cast<uint32_t>(attachment_descriptions.size());
        render_pass_create_info.pAttachments = attachment_descriptions.data();
        render_pass_create_info.subpassCount =
            static_cast<uint32_t>(actual_sub_passes.size());
        render_pass_create_info.pSubpasses = sub_pass_descriptions.data();
        render_pass_create_info.dependencyCount =
            static_cast<uint32_t>(dependencies.size());
        render_pass_create_info.pDependencies = dependencies.data();
        m_render_pass = device.get().createRenderPass(render_pass_create_info);
        LOG_INFO("Vulkan render pass created successfully");
        LOG_TRACE("RenderPass: attachment count: {}, subpass count: {}",
            m_attachments.size(), actual_sub_passes.size());
    }

    RenderPass::~RenderPass() {
        m_device.get().destroyRenderPass(m_render_pass);
    }
}
