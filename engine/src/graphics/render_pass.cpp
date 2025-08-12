#include "render_pass.h"
#include "device.h"

namespace Comet {
    static bool s_need_depth_sampling = false;

    RenderPass::RenderPass(Device* device, const std::vector<Attachment>& attachments, const std::vector<RenderSubPass>& sub_passes)
        : m_device(device),m_attachments(attachments), m_sub_passes(sub_passes) {
        // 1. default subpass and attachment
        if(sub_passes.empty() && attachments.empty()) {
            const Attachment attachment = {
                .format        = device->get_settings().surface_format,
                .load_op        = vk::AttachmentLoadOp::eClear,
                .store_op       = vk::AttachmentStoreOp::eStore,
                .stencil_load_op = vk::AttachmentLoadOp::eDontCare,
                .stencil_store_op= vk::AttachmentStoreOp::eDontCare,
                .final_layout   = vk::ImageLayout::ePresentSrcKHR,
                .usage         = vk::ImageUsageFlagBits::eColorAttachment
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
        std::vector<vk::SubpassDescription> sub_pass_descriptions(m_sub_passes.size());
        std::vector<vk::AttachmentReference> resolve_attachments_reference(m_sub_passes.size());
        
        // Store all attachment references with proper lifetime
        std::vector<std::vector<vk::AttachmentReference>> all_input_attachments_reference(m_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>> all_color_attachments_reference(m_sub_passes.size());
        std::vector<std::vector<vk::AttachmentReference>> all_depth_stencil_attachments_reference(m_sub_passes.size());
        
        for(int32_t i = 0; i < m_sub_passes.size(); ++i) {
            auto [input_attachments, color_attachments, depth_stencil_attachments, sample_count] = m_sub_passes[i];
            
            for(const auto& attachment : input_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_input_attachments_reference[i].emplace_back(reference);
            }
            
            for(const auto& attachment : color_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_color_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].samples = sample_count;
                if(sample_count > vk::SampleCountFlagBits::e1){
                    m_attachments[attachment.index].final_layout = attachment.layout;
                }
            }
            
            for(const auto& attachment : depth_stencil_attachments) {
                vk::AttachmentReference reference = {attachment.index, attachment.layout};
                all_depth_stencil_attachments_reference[i].emplace_back(reference);
                m_attachments[attachment.index].samples = sample_count;
                if(s_need_depth_sampling) {
                    m_attachments[attachment.index].final_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                } else {
                    m_attachments[attachment.index].final_layout = attachment.layout;
                }
            }

            if(sample_count > vk::SampleCountFlagBits::e1) {
                Attachment msaa_attachment = {
                    .format = device->get_settings().surface_format,
                    .load_op = vk::AttachmentLoadOp::eDontCare,
                    .store_op = vk::AttachmentStoreOp::eStore,
                    .stencil_load_op = vk::AttachmentLoadOp::eDontCare,
                    .stencil_store_op = vk::AttachmentStoreOp::eDontCare,
                    .initial_layout = vk::ImageLayout::eUndefined,
                    .final_layout = vk::ImageLayout::ePresentSrcKHR,
                    .samples = vk::SampleCountFlagBits::e1,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment
                };
                m_attachments.push_back(msaa_attachment);
                vk::AttachmentReference reference = {static_cast<uint32_t>(m_attachments.size() - 1), vk::ImageLayout::eColorAttachmentOptimal};
                resolve_attachments_reference[i] = reference;
            }

            sub_pass_descriptions[i].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            sub_pass_descriptions[i].inputAttachmentCount = all_input_attachments_reference[i].size();
            sub_pass_descriptions[i].pInputAttachments = all_input_attachments_reference[i].empty() ? nullptr : all_input_attachments_reference[i].data();
            sub_pass_descriptions[i].colorAttachmentCount = all_color_attachments_reference[i].size();
            sub_pass_descriptions[i].pColorAttachments = all_color_attachments_reference[i].empty() ? nullptr : all_color_attachments_reference[i].data();
            sub_pass_descriptions[i].pResolveAttachments = (sample_count > vk::SampleCountFlagBits::e1 ? &resolve_attachments_reference[i]: nullptr);
            sub_pass_descriptions[i].pDepthStencilAttachment = all_depth_stencil_attachments_reference[i].empty() ? nullptr : all_depth_stencil_attachments_reference[i].data();
            sub_pass_descriptions[i].preserveAttachmentCount = 0;
            sub_pass_descriptions[i].pPreserveAttachments = nullptr;
        }
        std::vector<vk::SubpassDependency> dependencies(m_sub_passes.size() - 1);
        if(m_sub_passes.size() > 1) {
            for(uint32_t i = 0; i < dependencies.size(); ++i) {
                dependencies[i].srcSubpass         = i;
                dependencies[i].dstSubpass         = i + 1;
                dependencies[i].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dependencies[i].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
                dependencies[i].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
                dependencies[i].dstAccessMask   = vk::AccessFlagBits::eInputAttachmentRead;
                dependencies[i].dependencyFlags = vk::DependencyFlagBits::eByRegion;
            }
        }
        // 3. create info
        std::vector<vk::AttachmentDescription> attachment_descriptions;
        for(const auto& attachment : m_attachments ) {
            vk::AttachmentDescription description = {};
            description.format = attachment.format;
            description.samples = attachment.samples;
            description.loadOp = attachment.load_op;
            description.storeOp = attachment.store_op;
            description.stencilLoadOp = attachment.stencil_load_op;
            description.stencilStoreOp = attachment.stencil_store_op;
            description.initialLayout = attachment.initial_layout;
            description.finalLayout = attachment.final_layout;
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
        LOG_TRACE("RenderPass: attachment count: {}, subpass count: {}",  m_attachments.size(), m_sub_passes.size());
    }
    RenderPass::~RenderPass() {
        m_device->get_device().destroyRenderPass(m_render_pass);
    }
}
