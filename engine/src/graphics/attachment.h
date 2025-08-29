#pragma once
#include "vk_common.h"

namespace Comet {
    struct Attachment {

        static Attachment get_color_attachment(const Format format,
            const SampleCount sample_count = SampleCount::Count1) {
            Attachment attachment;
            attachment.description.format = format;
            attachment.description.samples = sample_count;
            attachment.description.load_op = AttachmentLoadOp::Clear;
            attachment.description.stencil_load_op = AttachmentLoadOp::DontCare;
            attachment.description.store_op = AttachmentStoreOp::DontCare;
            attachment.description.initial_layout = ImageLayout::Undefined;
            if(sample_count > SampleCount::Count1) {
                attachment.description.final_layout = ImageLayout::ColorAttachmentOptimal;
                attachment.description.stencil_store_op = AttachmentStoreOp::DontCare;
            } else {
                attachment.description.stencil_store_op = AttachmentStoreOp::Store;
                attachment.description.final_layout = ImageLayout::PresentSrcKHR;
            }
            attachment.usage = Flags<ImageUsage>(ImageUsage::ColorAttachment);
            return attachment;
        }

        static Attachment get_depth_attachment(const Format format,
            const SampleCount sample_count = SampleCount::Count1) {
            Attachment attachment;
            attachment.description.format = format;
            attachment.description.samples = sample_count;
            attachment.description.load_op = AttachmentLoadOp::Clear;
            attachment.description.store_op = AttachmentStoreOp::DontCare;
            attachment.description.stencil_load_op = AttachmentLoadOp::DontCare;
            attachment.description.stencil_store_op = AttachmentStoreOp::DontCare;
            attachment.description.initial_layout = ImageLayout::Undefined;
            attachment.description.final_layout = ImageLayout::DepthStencilAttachmentOptimal;
            attachment.usage = Flags<ImageUsage>(ImageUsage::DepthStencilAttachment);
            return attachment;
        }

        struct Description {
            Format format = Format::UNDEFINED;
            SampleCount samples = SampleCount::Count1;
            AttachmentLoadOp load_op = AttachmentLoadOp::DontCare;
            AttachmentStoreOp store_op = AttachmentStoreOp::DontCare;
            AttachmentLoadOp stencil_load_op = AttachmentLoadOp::DontCare;
            AttachmentStoreOp stencil_store_op = AttachmentStoreOp::DontCare;
            ImageLayout initial_layout = ImageLayout::Undefined;
            ImageLayout final_layout = ImageLayout::Undefined;
        } description;
        Flags<ImageUsage> usage = Flags<ImageUsage>(ImageUsage::ColorAttachment);
    };

    class SubpassAttachment{
    public:
        enum class Type { Input, Color, DepthStencil };

        SubpassAttachment(const uint32_t index, const Type type) : index(index){
            switch(type) {
                case Type::Input:
                    layout = ImageLayout::ShaderReadOnlyOptimal;
                    break;
                case Type::Color:
                    layout = ImageLayout::ColorAttachmentOptimal;
                    break;
                case Type::DepthStencil:
                    layout = ImageLayout::DepthStencilAttachmentOptimal;
                    break;
            }
        }
        SubpassAttachment(const uint32_t index, const ImageLayout layout)
            : index(index), layout(layout) {}

        uint32_t index;
        ImageLayout layout;
    };

    class SubpassInputAttachment:public SubpassAttachment {
    public:
        explicit SubpassInputAttachment(const uint32_t index) : SubpassAttachment(index, ImageLayout::ShaderReadOnlyOptimal) {}
    };
    class SubpassColorAttachment: public SubpassAttachment {
    public:
        explicit SubpassColorAttachment(const uint32_t index) : SubpassAttachment(index, ImageLayout::ColorAttachmentOptimal) {}
    };
    class SubpassDepthStencilAttachment: public SubpassAttachment {
    public:
        explicit SubpassDepthStencilAttachment(const uint32_t index) : SubpassAttachment(index, ImageLayout::DepthStencilAttachmentOptimal) {}
    };

}
