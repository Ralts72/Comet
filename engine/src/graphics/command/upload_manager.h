#pragma once

#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "graphics/synchronization/resource_state.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Comet {
    class Buffer;
    class CommandContext;
    class CPUBuffer;
    class Device;
    class Image;

    class COMET_API UploadManager {
    public:
        struct CreateInfo {
            size_t staging_page_size = 4 * 1024 * 1024;
            size_t max_cached_staging_pages = 4;
            uint32_t memory_pressure_threshold_percent = 90;
        };

        explicit UploadManager(Device& device);
        UploadManager(Device& device, CreateInfo create_info);
        ~UploadManager();

        UploadManager(const UploadManager&) = delete;
        UploadManager& operator=(const UploadManager&) = delete;
        UploadManager(UploadManager&&) noexcept = delete;
        UploadManager& operator=(UploadManager&&) noexcept = delete;

        void enqueue_upload(
            std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data,
            const ResourceState& after);

        [[nodiscard]] GpuResourceResult<void> try_enqueue_upload(
            std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data,
            const ResourceState& after,
            bool within_budget);

        void enqueue_upload(
            std::shared_ptr<Image> destination,
            std::span<const std::byte> data,
            const ImageState& before,
            const ImageState& after);

        [[nodiscard]] GpuResourceResult<void> try_enqueue_upload(
            std::shared_ptr<Image> destination,
            std::span<const std::byte> data,
            const ImageState& before,
            const ImageState& after,
            bool within_budget);

        [[nodiscard]] std::optional<GpuCompletionPoint> flush_batch();
        void abort_batch();
        void upload_and_wait();
        void collect_completed();

    private:
        struct StagingPage {
            std::shared_ptr<CPUBuffer> buffer;
            size_t capacity = 0;
            size_t used = 0;
        };

        struct StagingAllocation {
            StagingPage* page = nullptr;
            size_t offset = 0;
        };

        struct BatchResources {
            std::vector<std::shared_ptr<Buffer>> buffers;
            std::vector<std::shared_ptr<Image>> images;
            std::vector<std::unique_ptr<StagingPage>> staging_pages;
        };

        struct PendingBatch {
            std::unique_ptr<CommandContext> context;
            BatchResources resources;
            GpuCompletionPoint completion;
        };

        [[nodiscard]] CommandContext& get_active_context();
        [[nodiscard]] GpuResourceResult<StagingAllocation>
        try_allocate_staging(
            std::span<const std::byte> data,
            bool within_budget);
        void prepare_for_staging_growth(size_t capacity);
        void recycle_staging_pages(BatchResources& resources);
        void wait_for_pending_batches();

        Device& m_device;
        CreateInfo m_create_info;
        std::unique_ptr<CommandContext> m_active_context;
        BatchResources m_active_resources;
        std::vector<PendingBatch> m_pending_batches;
        std::vector<std::unique_ptr<StagingPage>> m_available_pages;
        bool m_memory_pressure_reported = false;
    };
}
