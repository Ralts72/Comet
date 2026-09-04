#pragma once

#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "graphics/synchronization/resource_state.h"

#include <cstddef>
#include <functional>
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
    class UploadBatch;

    class COMET_API UploadManager {
    public:
        using StagingGrowthGuard =
            std::function<std::optional<vk::Result>(size_t capacity, bool within_budget)>;

        struct CreateInfo {
            size_t staging_page_size = 4 * 1024 * 1024;
            size_t max_cached_staging_pages = 4;
            uint32_t memory_pressure_threshold_percent = 90;
            StagingGrowthGuard staging_growth_guard;
        };

        explicit UploadManager(Device& device);
        UploadManager(Device& device, CreateInfo create_info);
        ~UploadManager();

        UploadManager(const UploadManager&) = delete;
        UploadManager& operator=(const UploadManager&) = delete;
        UploadManager(UploadManager&&) noexcept = delete;
        UploadManager& operator=(UploadManager&&) noexcept = delete;

        [[nodiscard]] UploadBatch begin_batch();
        void collect_completed();

    private:
        friend class UploadBatch;

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

        [[nodiscard]] GpuResourceResult<StagingAllocation> try_allocate_staging(
            BatchResources& resources, std::span<const std::byte> data,
            bool within_budget);
        [[nodiscard]] GpuCompletionPoint submit_batch(UploadBatch& batch);
        void abort_batch(UploadBatch& batch);
        void prepare_for_staging_growth(size_t capacity);
        void recycle_staging_pages(BatchResources& resources);
        void wait_for_pending_batches();

        Device& m_device;
        CreateInfo m_create_info;
        std::vector<PendingBatch> m_pending_batches;
        std::vector<std::unique_ptr<StagingPage>> m_available_pages;
        size_t m_open_batch_count = 0;
        bool m_memory_pressure_reported = false;
    };

    class COMET_API UploadBatch final {
    public:
        ~UploadBatch();

        UploadBatch(const UploadBatch&) = delete;
        UploadBatch& operator=(const UploadBatch&) = delete;
        UploadBatch(UploadBatch&&) noexcept = delete;
        UploadBatch& operator=(UploadBatch&&) noexcept = delete;

        void enqueue_upload(std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data, const ResourceState& after);

        [[nodiscard]] GpuResourceResult<void> try_enqueue_upload(
            std::shared_ptr<Buffer> destination, std::span<const std::byte> data,
            const ResourceState& after, bool within_budget);

        void enqueue_upload(std::shared_ptr<Image> destination,
            std::span<const std::byte> data, const ImageState& before,
            const ImageState& after);

        [[nodiscard]] GpuResourceResult<void> try_enqueue_upload(
            std::shared_ptr<Image> destination, std::span<const std::byte> data,
            const ImageState& before, const ImageState& after, bool within_budget);

        [[nodiscard]] GpuCompletionPoint submit();
        void abort();

    private:
        friend class UploadManager;

        explicit UploadBatch(UploadManager& manager);

        void ensure_active() const;
        [[nodiscard]] CommandContext& get_context();

        UploadManager* m_manager;
        std::unique_ptr<CommandContext> m_context;
        UploadManager::BatchResources m_resources;
    };
}
