#include "render_pass.h"
#include "device.h"

namespace Comet {
    static bool s_need_depth_sampling = false;

    RenderPass::RenderPass(Device* device, const std::vector<Attachment>& attachments, const std::vector<RenderSubPass>& sub_passes)
        : m_device(device), m_attachments(attachments), m_sub_passes(sub_passes) {
        // 1. default subpass and attachment
        if(sub_passes.empty() && attachments.empty()) {
            vk::AttachmentDescription description{};
            description.format = device->get_settings().surface_format;
            description.loadOp = vk::AttachmentLoadOp::eClear;
            description.storeOp = vk::AttachmentStoreOp::eStore;
            description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            description.finalLayout = vk::ImageLayout::ePresentSrcKHR;
            Attachment attachment = {
                .description = description,
                .usage = vk::ImageUsageFlagBits::eColorAttachment
            };
            SubpassColorAttachment subpass_attachment(0);
            const RenderSubPass render_sub_pass = {
                .color_attachments = {subpass_attachment},
                .sample_count = vk::SampleCountFlagBits::e1
            };
            m_attachments.push_back(attachment);
            m_sub_passes.push_back(render_sub_pass);
        }
        // 2.subpass
        // index check
        for(const auto& sub_pass: m_sub_passes) {
            for(const auto& attachment: sub_pass.input_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("input attachment index exceeds attachment pool ");
                }
            }
            for(const auto& attachment: sub_pass.color_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("color attachment index exceeds attachment pool ");
                }
            }
            for(const auto& attachment: sub_pass.depth_stencil_attachments) {
                if(attachment.index >= m_attachments.size()) {
                    LOG_FATAL("depth stencil attachment index exceeds attachment pool ");
                }
            }
        }
        std::vector<vk::SubpassDescription> sub_pass_descriptions(m_sub_passes.size());
        std::vector<vk::AttachmentReference> resolve_attachments_reference(m_sub_passes.size());
        
        std::vector<std::vector<vk::AttachmentReference>> all_input_attachments_reference(m_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>> all_color_attachments_reference(m_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>> all_depth_stencil_attachments_reference(m_sub_passes.size());
        
        for(uint32_t i = 0; i < m_sub_passes.size(); ++i) {
            auto [input_attachments, color_attachments, depth_stencil_attachments, sample_count] = m_sub_passes[i];
            
            for(const auto& attachment : input_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_input_attachments_reference[i].emplace_back(reference);
            }
            
            for(const auto& attachment : color_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_color_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].description.samples = sample_count;
                if(sample_count > vk::SampleCountFlagBits::e1){
                    m_attachments[attachment.index].description.finalLayout = attachment.layout;
                }
            }
            
            for(const auto& attachment : depth_stencil_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_depth_stencil_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].description.samples = sample_count;
                if(s_need_depth_sampling) {
                    m_attachments[attachment.index].description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                } else {
                    m_attachments[attachment.index].description.finalLayout = attachment.layout;
                }
            }

            if(sample_count > vk::SampleCountFlagBits::e1) {
                vk::AttachmentDescription msaa_description{};
                msaa_description.format = device->get_settings().surface_format;
                msaa_description.loadOp = vk::AttachmentLoadOp::eDontCare;
                msaa_description.storeOp = vk::AttachmentStoreOp::eStore;
                msaa_description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
                msaa_description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
                msaa_description.initialLayout = vk::ImageLayout::eUndefined;
                msaa_description.finalLayout = vk::ImageLayout::ePresentSrcKHR;
                msaa_description.samples = vk::SampleCountFlagBits::e1;
                Attachment msaa_attachment = {
                    .description = msaa_description,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment
                };

                m_attachments.push_back(msaa_attachment);
                vk::AttachmentReference reference = {static_cast<uint32_t>(m_attachments.size() - 1), vk::ImageLayout::eColorAttachmentOptimal};
                resolve_attachments_reference[i] = reference;
            }

            sub_pass_descriptions[i].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            sub_pass_descriptions[i].inputAttachmentCount = all_input_attachments_reference[i].size();
            sub_pass_descriptions[i].pInputAttachments = all_input_attachments_reference[i].data();
            sub_pass_descriptions[i].colorAttachmentCount = all_color_attachments_reference[i].size();
            sub_pass_descriptions[i].pColorAttachments = all_color_attachments_reference[i].data();
            sub_pass_descriptions[i].pResolveAttachments = (sample_count > vk::SampleCountFlagBits::e1 ? &resolve_attachments_reference[i]: nullptr);
            sub_pass_descriptions[i].pDepthStencilAttachment = all_depth_stencil_attachments_reference[i].data();
            sub_pass_descriptions[i].preserveAttachmentCount = 0;
            sub_pass_descriptions[i].pPreserveAttachments = nullptr;
        }
        std::vector<vk::SubpassDependency> dependencies(m_sub_passes.size() - 1);
        if(m_sub_passes.size() > 1) {
            for(uint32_t j = 0; j < dependencies.size(); ++j) {
                dependencies[j].srcSubpass         = j;
                dependencies[j].dstSubpass         = j + 1;
                dependencies[j].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dependencies[j].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
                dependencies[j].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
                dependencies[j].dstAccessMask   = vk::AccessFlagBits::eInputAttachmentRead;
                dependencies[j].dependencyFlags = vk::DependencyFlagBits::eByRegion;
            }
        }
        // 3. create info
        std::vector<vk::AttachmentDescription> attachment_descriptions;
        attachment_descriptions.reserve(m_attachments.size());
        for(const auto& [description, usage] : m_attachments ) {
            attachment_descriptions.push_back(description);
        }
        vk::RenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
        render_pass_create_info.pAttachments = attachment_descriptions.data();
        render_pass_create_info.subpassCount = static_cast<uint32_t>(m_sub_passes.size());
        render_pass_create_info.pSubpasses = sub_pass_descriptions.data();
        render_pass_create_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        render_pass_create_info.pDependencies = dependencies.data();
        m_render_pass = device->get_device().createRenderPass(render_pass_create_info);
        LOG_INFO("Vulkan render pass created successfully");
        LOG_TRACE("RenderPass: attachment count: {}, subpass count: {}",  m_attachments.size(), m_sub_passes.size());
    }

    RenderPass::~RenderPass() {
        m_device->get_device().destroyRenderPass(m_render_pass);
    }
}
