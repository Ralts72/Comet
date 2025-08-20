#pragma once
#include "graphics/frame_buffer.h"
#include "graphics/render_pass.h"

namespace Comet {
    // struct RenderTargetInfo {
    //     vk::Extent2D extent;
    //     std::vector<vk::ClearValue> clear_values;
    //     uint32_t buffer_count;
    // };
    //
    // enum class RenderTargetType {
    //     Swapchain, Image
    // };
    //
    // class RenderTarget {
    // public:
    //     RenderTarget(RenderPass* render_pass);
    //     RenderTarget(RenderPass* render_pass, RenderTargetInfo info);
    //     ~RenderTarget();
    //
    // private:
    //     std::vector<std::shared_ptr<FrameBuffer>> m_frame_buffers;
    //     RenderPass* m_render_pass;
    //     RenderTargetInfo m_info;
    //     RenderTargetType m_type;
    // };

}
