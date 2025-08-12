#pragma once
#include "vk_common.h"
#include "attachment.h"

namespace Comet {
    class Device;

    struct RenderSubPass{
        std::vector<SubpassInputAttachment> input_attachments;
        std::vector<SubpassColorAttachment> color_attachments;
        std::vector<SubpassDepthStencilAttachment> depth_stencil_attachments;
        // std::vector<SubpassAttachment> resolve_attachments;
        vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1;
    };

    class RenderPass{
    public:
        explicit RenderPass(Device* device, const std::vector<Attachment>& attachments = {}, const std::vector<RenderSubPass>& sub_passes = {} );
        ~RenderPass();
        [[nodiscard]] vk::RenderPass get_render_pass() const { return m_render_pass; }
        [[nodiscard]] const std::vector<RenderSubPass>& get_sub_passes() const { return m_sub_passes; }
    private:
        vk::RenderPass m_render_pass;
        Device* m_device;
        std::vector<Attachment> m_attachments;
        std::vector<RenderSubPass> m_sub_passes;
    };
}
