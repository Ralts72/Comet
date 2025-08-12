#pragma once
#include "vk_common.h"

namespace Comet {
    struct Attachment {
        vk::Format                format           = vk::Format::eUndefined;
        vk::AttachmentLoadOp      load_op          = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp     store_op         = vk::AttachmentStoreOp::eDontCare;
        vk::AttachmentLoadOp      stencil_load_op  = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp     stencil_store_op = vk::AttachmentStoreOp::eDontCare;
        vk::ImageLayout           initial_layout   = vk::ImageLayout::eUndefined;
        vk::ImageLayout           final_layout     = vk::ImageLayout::eUndefined;
        vk::SampleCountFlagBits   samples          = vk::SampleCountFlagBits::e1;
        vk::ImageUsageFlags       usage            = vk::ImageUsageFlagBits::eColorAttachment;
    };
    enum class AttachmentType { Input, Color, DepthStencil };

    class SubpassAttachment{
    public:
        SubpassAttachment(const uint32_t index, const AttachmentType type) : index(index){
            switch(type) {
                case AttachmentType::Input:
                    layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    break;
                case AttachmentType::Color:
                    layout = vk::ImageLayout::eColorAttachmentOptimal;
                    break;
                case AttachmentType::DepthStencil:
                    layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                    break;
            }
        }
        SubpassAttachment(const uint32_t index, const vk::ImageLayout layout)
            : index(index), layout(layout) {}

        uint32_t index;
        vk::ImageLayout layout;
    };

    class SubpassInputAttachment:public SubpassAttachment {
    public:
        explicit SubpassInputAttachment(const uint32_t index) : SubpassAttachment(index, vk::ImageLayout::eShaderReadOnlyOptimal) {}
    };
    class SubpassColorAttachment: public SubpassAttachment {
    public:
        explicit SubpassColorAttachment(const uint32_t index) : SubpassAttachment(index, vk::ImageLayout::eColorAttachmentOptimal) {}
    };
    class SubpassDepthStencilAttachment: public SubpassAttachment {
    public:
        explicit SubpassDepthStencilAttachment(const uint32_t index) : SubpassAttachment(index, vk::ImageLayout::eDepthStencilAttachmentOptimal) {}
    };
}
