#include "RenderPass.h"

namespace Comet
{
    RenderPass::RenderPass(Context *context, const std::vector<Attachment> &attachments, const std::vector<SubPass> &subpasses) : m_contextHandle(context), m_attachments(attachments), m_subpasses(subpasses)
    {
        vk::RenderPassCreateInfo renderPassCreateInfo;
        // 1. attachment descriptions
        if (subpasses.empty())
        {
            if (attachments.empty())
            {
                vk::AttachmentDescription attachmentDescription;
                m_attachments = {{.format = context->getSwapchain()->getSurfaceInfo().format.format,
                                  .loadOp = vk::AttachmentLoadOp::eClear,
                                  .storeOp = vk::AttachmentStoreOp::eStore,
                                  .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                                  .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                                  .initLayout = vk::ImageLayout::eUndefined,
                                  .finalLayout = vk::ImageLayout::eUndefined,
                                  .samples = vk::SampleCountFlagBits::e1,
                                  .usage = vk::ImageUsageFlagBits::eColorAttachment}};
            }
            m_subpasses = {{.colorAttachments = {0}}};
        }

        // 2. subpass
        std::vector<vk::SubpassDescription> subpassDescriptions(subpasses.size());
        std::vector<std::vector<vk::AttachmentReference>> inputAttachmentRefs(subpasses.size());
        std::vector<std::vector<vk::AttachmentReference>> colorAttachmentRefs(subpasses.size());
        std::vector<std::vector<vk::AttachmentReference>> depthStencilAttachmentRefs(subpasses.size());

        std::vector<std::vector<vk::AttachmentReference>> resolveAttachmentRefs(subpasses.size());

        for (int i = 0; i < subpasses.size(); i++)
        {
            SubPass subpass = subpasses[i];
            for (const auto &attachment : subpass.inputAttachments)
            {
                inputAttachmentRefs[i].push_back({attachment, vk::ImageLayout::eShaderReadOnlyOptimal});
            }
            for (const auto &attachment : subpass.colorAttachments)
            {
                colorAttachmentRefs[i].push_back({attachment, vk::ImageLayout::eColorAttachmentOptimal});
                m_attachments[attachment].samples = subpass.sampleCount;
            }
        }
    }
}