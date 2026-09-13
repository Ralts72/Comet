#pragma once
#include "graphics/creation.h"
#include "attachment.h"
#include "common/export.h"

#include <memory>
#include <vector>

namespace Comet {
    class Device;

    struct RenderSubPass {
        std::vector<SubpassInputAttachment> input_attachments;
        std::vector<SubpassColorAttachment> color_attachments;
        std::vector<SubpassDepthStencilAttachment> depth_stencil_attachments;
        SampleCount sample_count = SampleCount::Count1;
        ImageLayout resolve_final_layout = ImageLayout::PresentSrcKHR;
        Flags<ImageUsage> resolve_usage = Flags<ImageUsage>(ImageUsage::ColorAttachment);
    };

    class COMET_API RenderPass {
    public:
        static Result<std::unique_ptr<RenderPass>, GraphicsError> create(Device& device,
            const std::vector<Attachment>& attachments = {},
            const std::vector<RenderSubPass>& sub_passes = {},
            Format surface_format = Format::B8G8R8A8_SRGB);
        ~RenderPass() = default;

        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;
        RenderPass(RenderPass&&) noexcept = delete;
        RenderPass& operator=(RenderPass&&) noexcept = delete;

        [[nodiscard]] vk::RenderPass get() const { return m_render_pass.get(); }
        [[nodiscard]] uint32_t get_subpass_count() const { return m_subpass_count; }
        [[nodiscard]] const std::vector<Attachment>& get_attachments() const {
            return m_attachments;
        }

    private:
        RenderPass(vk::UniqueRenderPass render_pass, std::vector<Attachment> attachments,
            uint32_t subpass_count);

        vk::UniqueRenderPass m_render_pass;
        std::vector<Attachment> m_attachments;
        uint32_t m_subpass_count = 0;
    };
}
