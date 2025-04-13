#pragma once
#include <vulkan/vulkan.hpp>
#include "../core/Context.h"

namespace Comet
{
    struct Attachment
    {
        vk::Format format = vk::Format::eUndefined;
        vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eDontCare;
        vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        vk::ImageLayout initLayout = vk::ImageLayout::eUndefined;
        vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined;
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment;
    };
    struct SubPass
    {
        std::vector<uint32_t> inputAttachments;
        std::vector<uint32_t> colorAttachments;
        std::vector<uint32_t> depthStencilAttachment;
        vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
    };
    class RenderPass
    {
    public:
        RenderPass(Context *context, const std::vector<Attachment> &attachments, const std::vector<SubPass> &subpasses);
        ~RenderPass() = default;

    private:
        std::vector<Attachment> m_attachments;
        std::vector<SubPass> m_subpasses;

        Context *m_contextHandle;

        vk::RenderPass m_renderPass;
    };
}