#pragma once
#include "vk_common.h"
#include "attachment.h"
#include "common/export.h"

namespace Comet {
    class Device;
    class FrameBuffer;

    struct RenderSubPass{
        std::vector<SubpassInputAttachment> input_attachments;
        std::vector<SubpassColorAttachment> color_attachments;
        std::vector<SubpassDepthStencilAttachment> depth_stencil_attachments;
        SampleCount sample_count = SampleCount::Count1;
    };

    class COMET_API RenderPass{
    public:
        explicit RenderPass(Device* device, const std::vector<Attachment>& attachments = {},
            const std::vector<RenderSubPass>& sub_passes = {} );
        ~RenderPass();

        [[nodiscard]] vk::RenderPass get() const { return m_render_pass; }
        [[nodiscard]] const std::vector<RenderSubPass>& get_sub_passes() const { return m_sub_passes; }
        [[nodiscard]] const std::vector<Attachment>& get_attachments() const { return m_attachments; }
        [[nodiscard]] uint32_t get_attachments_count() const { return static_cast<uint32_t>(m_attachments.size()); }

    private:
        vk::RenderPass m_render_pass;
        Device* m_device;
        std::vector<Attachment> m_attachments;
        std::vector<RenderSubPass> m_sub_passes;
    };
}
