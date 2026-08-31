#include "graphics/command/upload_manager.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"

#include <algorithm>

namespace Comet {
    UploadManager::UploadManager(Device& device) : m_device(device) {}

    UploadManager::~UploadManager() {
        if(m_open_batch_count != 0) {
            LOG_FATAL(
                "UploadManager destroyed with {} open batch(es)",
                m_open_batch_count);
        }
        wait_for_pending_batches();
    }

    UploadBatch UploadManager::begin_batch() {
        return UploadBatch(*this);
    }

    UploadBatch::UploadBatch(UploadManager& manager)
        : m_manager(&manager) {
        ++m_manager->m_open_batch_count;
    }

    UploadBatch::~UploadBatch() {
        abort();
    }

    void UploadBatch::enqueue_upload(
        std::shared_ptr<Buffer> destination,
        const std::span<const std::byte> data,
        const ResourceState& after,
        const std::string_view debug_name) {
        ensure_active();
        if(!destination || data.empty()) {
            LOG_FATAL("Buffer upload requires a destination and non-empty data");
        }
        if(data.size_bytes() != destination->get_size()) {
            LOG_FATAL(
                "Buffer upload size {} does not match destination size {}",
                data.size_bytes(),
                destination->get_size());
        }

        const std::string_view resolved_name =
            debug_name.empty() ? "buffer upload" : debug_name;
        auto staging = Buffer::create_upload_buffer(
            m_manager->m_device,
            Flags<BufferUsage>(BufferUsage::CopySrc),
            data.size_bytes(),
            data.data(),
            resolved_name);
        get_context().copy_buffer(
            *staging,
            *destination,
            data.size_bytes());
        const auto transfer = resolve_resource_state(
            ResourceUsage::TransferDestination,
            {},
            after.queue_family);
        if(!transfer) {
            LOG_FATAL("UploadManager failed to resolve buffer transfer state");
        }
        get_context().transition_buffer_state(
            *destination,
            *transfer,
            after,
            0,
            data.size_bytes());
        m_resources.buffers.push_back(std::move(staging));
        m_resources.buffers.push_back(std::move(destination));
    }

    void UploadBatch::enqueue_upload(
        std::shared_ptr<Image> destination,
        const std::span<const std::byte> data,
        const ImageState& before,
        const ImageState& after,
        const std::string_view debug_name) {
        ensure_active();
        if(!destination || data.empty()) {
            LOG_FATAL("Image upload requires a destination and non-empty data");
        }
        const auto info = destination->get_info();
        const auto& range = before.subresources;
        if(before.subresources != after.subresources
           || before.resource.queue_family != after.resource.queue_family
           || range.aspects != ImageAspect::Color
           || range.base_mip_level != 0
           || range.level_count != 1
           || range.base_array_layer != 0
           || range.layer_count != 1
           || !static_cast<bool>(info.usage & ImageUsage::CopyDst)) {
            LOG_FATAL(
                "Image upload currently requires one full color subresource "
                "with stable queue ownership and CopyDst usage");
        }

        const std::string_view resolved_name =
            debug_name.empty() ? "image upload" : debug_name;
        auto staging = Buffer::create_upload_buffer(
            m_manager->m_device,
            Flags<BufferUsage>(BufferUsage::CopySrc),
            data.size_bytes(),
            data.data(),
            resolved_name);

        auto& context = get_context();
        const auto transfer = resolve_image_state(
            ResourceUsage::TransferDestination,
            before.subresources,
            {},
            before.resource.queue_family);
        if(!transfer) {
            LOG_FATAL("UploadManager failed to resolve image transfer state");
        }
        context.transition_image_state(*destination, before, *transfer);
        context.copy_buffer_to_image(
            *staging,
            *destination,
            transfer->layout,
            vk::Extent3D{info.extent.x, info.extent.y, info.extent.z});
        context.transition_image_state(*destination, *transfer, after);

        m_resources.buffers.push_back(std::move(staging));
        m_resources.images.push_back(std::move(destination));
    }

    GpuCompletionPoint UploadBatch::submit() {
        ensure_active();
        if(!m_context) {
            LOG_FATAL("Cannot submit an empty upload batch");
        }
        return m_manager->submit_batch(*this);
    }

    GpuCompletionPoint UploadManager::submit_batch(UploadBatch& batch) {
        auto context = std::move(batch.m_context);
        const auto completion = context->submit();
        if(!completion.is_valid()) {
            LOG_FATAL("UploadManager failed to submit an active batch");
        }
        m_pending_batches.push_back({
            .context = std::move(context),
            .resources = std::move(batch.m_resources),
            .completion = completion
        });
        batch.m_resources = {};
        --m_open_batch_count;
        batch.m_manager = nullptr;
        return completion;
    }

    void UploadBatch::abort() {
        if(!m_manager) {
            return;
        }
        m_manager->abort_batch(*this);
    }

    void UploadManager::abort_batch(UploadBatch& batch) {
        if(batch.m_context) {
            batch.m_context->discard();
            batch.m_context.reset();
        }
        batch.m_resources = {};
        --m_open_batch_count;
        batch.m_manager = nullptr;
    }

    void UploadManager::collect_completed() {
        std::erase_if(
            m_pending_batches,
            [](const PendingBatch& batch) {
                return batch.completion.is_complete();
            });
    }

    void UploadBatch::ensure_active() const {
        if(!m_manager) {
            LOG_FATAL("UploadBatch is no longer active");
        }
    }

    CommandContext& UploadBatch::get_context() {
        ensure_active();
        if(!m_context) {
            m_context = m_manager->m_device.create_command_context();
        }
        return *m_context;
    }

    void UploadManager::wait_for_pending_batches() {
        for(const auto& batch: m_pending_batches) {
            batch.completion.wait();
        }
        m_pending_batches.clear();
    }
}
