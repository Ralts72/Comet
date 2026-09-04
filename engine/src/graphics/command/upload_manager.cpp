#include "graphics/command/upload_manager.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/memory_budget.h"

#include <algorithm>
#include <limits>

namespace Comet {
    namespace {
        constexpr size_t STAGING_ALIGNMENT = 4;

        size_t align_staging_offset(const size_t value) {
            constexpr size_t padding = STAGING_ALIGNMENT - 1;
            if(value > std::numeric_limits<size_t>::max() - padding) {
                LOG_FATAL("Upload staging offset overflow");
            }
            return (value + padding) & ~padding;
        }
    }

    UploadManager::UploadManager(Device& device) : UploadManager(device, CreateInfo{}) {}

    UploadManager::UploadManager(Device& device, const CreateInfo create_info)
        : m_device(device), m_create_info(create_info) {
        if(m_create_info.staging_page_size < STAGING_ALIGNMENT) {
            LOG_FATAL("UploadManager staging page size is too small");
        }
        if(m_create_info.memory_pressure_threshold_percent == 0
            || m_create_info.memory_pressure_threshold_percent > 100) {
            LOG_FATAL("UploadManager memory pressure threshold must be in [1, 100]");
        }
        m_create_info.staging_page_size =
            align_staging_offset(m_create_info.staging_page_size);
    }

    UploadManager::~UploadManager() {
        if(m_open_batch_count != 0) {
            LOG_FATAL(
                "UploadManager destroyed with {} open batch(es)", m_open_batch_count);
        }
        wait_for_pending_batches();
    }

    UploadBatch UploadManager::begin_batch() {
        return UploadBatch(*this);
    }

    UploadBatch::UploadBatch(UploadManager& manager) : m_manager(&manager) {
        ++m_manager->m_open_batch_count;
    }

    UploadBatch::~UploadBatch() {
        abort();
    }

    void UploadBatch::enqueue_upload(std::shared_ptr<Buffer> destination,
        const std::span<const std::byte> data, const ResourceState& after) {
        const auto result =
            try_enqueue_upload(std::move(destination), data, after, false);
        if(!result) {
            LOG_FATAL("Failed to allocate buffer upload staging memory: {}",
                vk::to_string(result.result()));
        }
    }

    GpuResourceResult<void> UploadBatch::try_enqueue_upload(
        std::shared_ptr<Buffer> destination, const std::span<const std::byte> data,
        const ResourceState& after, const bool within_budget) {
        ensure_active();
        if(!destination || data.empty()) {
            LOG_FATAL("Buffer upload requires a destination and non-empty data");
        }
        if(data.size_bytes() != destination->get_size()) {
            LOG_FATAL("Buffer upload size {} does not match destination size {}",
                data.size_bytes(), destination->get_size());
        }
        if(data.size_bytes() % STAGING_ALIGNMENT != 0) {
            LOG_FATAL("Buffer upload size must be a multiple of 4 bytes");
        }

        auto staging_attempt =
            m_manager->try_allocate_staging(m_resources, data, within_budget);
        if(!staging_attempt) {
            abort();
            return GpuResourceResult<void>::failure(staging_attempt.result());
        }
        const auto staging = std::move(staging_attempt).value();
        get_context().copy_buffer(
            *staging.page->buffer, *destination, data.size_bytes(), staging.offset);
        const auto transfer = resolve_resource_state(
            ResourceUsage::TransferDestination, {}, after.queue_family);
        if(!transfer) {
            LOG_FATAL("UploadManager failed to resolve buffer transfer state");
        }
        get_context().transition_buffer_state(
            *destination, *transfer, after, 0, data.size_bytes());
        m_resources.buffers.push_back(std::move(destination));
        return GpuResourceResult<void>::success();
    }

    void UploadBatch::enqueue_upload(std::shared_ptr<Image> destination,
        const std::span<const std::byte> data, const ImageState& before,
        const ImageState& after) {
        const auto result =
            try_enqueue_upload(std::move(destination), data, before, after, false);
        if(!result) {
            LOG_FATAL("Failed to allocate image upload staging memory: {}",
                vk::to_string(result.result()));
        }
    }

    GpuResourceResult<void> UploadBatch::try_enqueue_upload(
        std::shared_ptr<Image> destination, const std::span<const std::byte> data,
        const ImageState& before, const ImageState& after, const bool within_budget) {
        ensure_active();
        if(!destination || data.empty()) {
            LOG_FATAL("Image upload requires a destination and non-empty data");
        }
        const auto info = destination->get_info();
        const auto& range = before.subresources;
        if(before.subresources != after.subresources
            || before.resource.queue_family != after.resource.queue_family
            || range.aspects != ImageAspect::Color || range.base_mip_level != 0
            || range.level_count != 1 || range.base_array_layer != 0
            || range.layer_count != 1
            || !static_cast<bool>(info.usage & ImageUsage::CopyDst)) {
            LOG_FATAL("Image upload currently requires one full color subresource "
                      "with stable queue ownership and CopyDst usage");
        }

        auto staging_attempt =
            m_manager->try_allocate_staging(m_resources, data, within_budget);
        if(!staging_attempt) {
            abort();
            return GpuResourceResult<void>::failure(staging_attempt.result());
        }
        const auto staging = std::move(staging_attempt).value();
        auto& context = get_context();
        const auto transfer = resolve_image_state(ResourceUsage::TransferDestination,
            before.subresources, {}, before.resource.queue_family);
        if(!transfer) {
            LOG_FATAL("UploadManager failed to resolve image transfer state");
        }
        context.transition_image_state(*destination, before, *transfer);
        context.copy_buffer_to_image(*staging.page->buffer, *destination,
            transfer->layout, vk::Extent3D{info.extent.x, info.extent.y, info.extent.z},
            0, 1, 0, staging.offset);
        context.transition_image_state(*destination, *transfer, after);

        m_resources.images.push_back(std::move(destination));
        return GpuResourceResult<void>::success();
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
        m_pending_batches.push_back({.context = std::move(context),
            .resources = std::move(batch.m_resources),
            .completion = completion});
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
        recycle_staging_pages(batch.m_resources);
        batch.m_resources = {};
        --m_open_batch_count;
        batch.m_manager = nullptr;
    }

    void UploadManager::collect_completed() {
        for(auto batch = m_pending_batches.begin(); batch != m_pending_batches.end();) {
            if(!batch->completion.is_complete()) {
                ++batch;
                continue;
            }
            recycle_staging_pages(batch->resources);
            batch = m_pending_batches.erase(batch);
        }
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

    GpuResourceResult<UploadManager::StagingAllocation> UploadManager::
        try_allocate_staging(BatchResources& resources,
            const std::span<const std::byte> data, const bool within_budget) {
        collect_completed();

        for(const auto& page : resources.staging_pages) {
            const size_t offset = align_staging_offset(page->used);
            if(offset <= page->capacity && data.size_bytes() <= page->capacity - offset) {
                page->buffer->write(data.data(), data.size_bytes(), offset);
                page->used = offset + data.size_bytes();
                return GpuResourceResult<StagingAllocation>::success(
                    {.page = page.get(), .offset = offset});
            }
        }

        const size_t required_capacity = align_staging_offset(data.size_bytes());
        auto available = m_available_pages.end();
        for(auto candidate = m_available_pages.begin();
            candidate != m_available_pages.end(); ++candidate) {
            if((*candidate)->capacity >= required_capacity
                && (available == m_available_pages.end()
                    || (*candidate)->capacity < (*available)->capacity)) {
                available = candidate;
            }
        }

        std::unique_ptr<StagingPage> page;
        if(available != m_available_pages.end()) {
            page = std::move(*available);
            m_available_pages.erase(available);
        } else {
            const size_t capacity =
                std::max(m_create_info.staging_page_size, required_capacity);
            if(m_create_info.staging_growth_guard) {
                const auto rejection =
                    m_create_info.staging_growth_guard(capacity, within_budget);
                if(rejection) {
                    return GpuResourceResult<StagingAllocation>::failure(*rejection);
                }
            }
            prepare_for_staging_growth(capacity);
            auto buffer = Buffer::try_create_upload_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::CopySrc), capacity, within_budget,
                nullptr, "upload staging page");
            if(!buffer) {
                return GpuResourceResult<StagingAllocation>::failure(buffer.result());
            }
            page = std::make_unique<StagingPage>(
                StagingPage{.buffer = std::move(buffer).value(), .capacity = capacity});
        }

        page->buffer->write(data.data(), data.size_bytes(), 0);
        page->used = data.size_bytes();
        auto* allocation_page = page.get();
        resources.staging_pages.push_back(std::move(page));
        return GpuResourceResult<StagingAllocation>::success(
            {.page = allocation_page, .offset = 0});
    }

    void UploadManager::prepare_for_staging_growth(const size_t capacity) {
        const auto snapshot = m_device.query_memory_budget();
        const bool under_pressure = std::ranges::any_of(
            snapshot.heaps, [this, capacity](const MemoryHeapBudget& heap) {
                return heap.reaches_usage_percentage(
                    capacity, m_create_info.memory_pressure_threshold_percent);
            });

        if(!under_pressure) {
            m_memory_pressure_reported = false;
            return;
        }

        const size_t released_pages = m_available_pages.size();
        m_available_pages.clear();
        if(!m_memory_pressure_reported) {
            LOG_WARN("GPU memory budget pressure before growing upload staging "
                     "pool by {} bytes ({} budget); released {} idle page(s)",
                capacity, snapshot.driver_reported ? "driver-reported" : "estimated",
                released_pages);
            m_memory_pressure_reported = true;
        }
    }

    void UploadManager::recycle_staging_pages(BatchResources& resources) {
        for(auto& page : resources.staging_pages) {
            page->used = 0;
            if(page->capacity <= m_create_info.staging_page_size
                && m_available_pages.size() < m_create_info.max_cached_staging_pages) {
                m_available_pages.push_back(std::move(page));
            }
        }
        resources.staging_pages.clear();
    }

    void UploadManager::wait_for_pending_batches() {
        for(auto& batch : m_pending_batches) {
            batch.completion.wait();
            recycle_staging_pages(batch.resources);
        }
        m_pending_batches.clear();
    }
}
