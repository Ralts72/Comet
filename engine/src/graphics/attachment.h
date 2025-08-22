#pragma once
#include "vk_common.h"

namespace Comet {
    struct Attachment {

        static Attachment get_color_attachment(const vk::Format format,
            const vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1) {
            Attachment attachment;
            attachment.description.format = format;
            attachment.description.samples = sample_count;
            attachment.description.loadOp = vk::AttachmentLoadOp::eClear;
            attachment.description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachment.description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachment.description.initialLayout = vk::ImageLayout::eUndefined;
            if(sample_count > vk::SampleCountFlagBits::e1) {
                attachment.description.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
                attachment.description.storeOp = vk::AttachmentStoreOp::eDontCare;
            } else {
                attachment.description.storeOp = vk::AttachmentStoreOp::eStore;
                attachment.description.finalLayout = vk::ImageLayout::ePresentSrcKHR;
            }
            attachment.usage = vk::ImageUsageFlagBits::eColorAttachment;
            return attachment;
        }

        static Attachment get_depth_attachment(const vk::Format format,
            const vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1) {
            Attachment attachment;
            attachment.description.format = format;
            attachment.description.samples = sample_count;
            attachment.description.loadOp = vk::AttachmentLoadOp::eClear;
            attachment.description.storeOp = vk::AttachmentStoreOp::eDontCare;
            attachment.description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachment.description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachment.description.initialLayout = vk::ImageLayout::eUndefined;
            attachment.description.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            attachment.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            return attachment;
        }

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

    // color_attachment.format = s_vk_settings.surface_format;
    // color_attachment.samples = vk::SampleCountFlagBits::e4;
    // color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    // color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    // color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    // color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    // color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    // // color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal; // MSAA附件不应该直接用于呈现
    // color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
    // Attachment msaa_color_attachment = {
    //     .description = {
    //         .format =
    //     };
    // };


}
