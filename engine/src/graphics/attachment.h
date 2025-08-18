#pragma once
#include "vk_common.h"

namespace Comet {
    struct Attachment {
        vk::AttachmentDescription description{
            {},                                  // flags
            vk::Format::eUndefined,                 // format
            vk::SampleCountFlagBits::e1,            // samples
            vk::AttachmentLoadOp::eDontCare,        // loadOp
            vk::AttachmentStoreOp::eDontCare,       // storeOp
            vk::AttachmentLoadOp::eDontCare,        // stencilLoadOp
            vk::AttachmentStoreOp::eDontCare,       // stencilStoreOp
            vk::ImageLayout::eUndefined,            // initialLayout
            vk::ImageLayout::eUndefined             // finalLayout
        };
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment;
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
