#include "render_target.h"

namespace Comet {
    // RenderTarget::RenderTarget(RenderPass* render_pass): m_render_pass(render_pass), m_type(RenderTargetType::Swapchain) {
    //     // 默认构造函数，使用交换链作为渲染目标
    //     const RenderTargetInfo info = {
    //         .extent = {800, 600}, // 默认大小
    //         .clear_values = {
    //             vk::ClearValue(vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f}))),
    //             vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
    //         },
    //         .buffer_count = 2 // 默认双缓冲
    //     };
    //     m_info = info;
    //
    // }
    // RenderTarget::RenderTarget(RenderPass* render_pass, RenderTargetInfo info) {}
    // RenderTarget::~RenderTarget() {}
}
