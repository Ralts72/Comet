#pragma once

#include "common/export.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "graphics/synchronization/resource_state.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Comet {
    class Buffer;
    class CommandContext;
    class Device;
    class Image;
    class UploadBatch;

    class COMET_API UploadManager {
    public:
        explicit UploadManager(Device& device);
        ~UploadManager();

        UploadManager(const UploadManager&) = delete;
        UploadManager& operator=(const UploadManager&) = delete;
        UploadManager(UploadManager&&) noexcept = delete;
        UploadManager& operator=(UploadManager&&) noexcept = delete;

        [[nodiscard]] UploadBatch begin_batch();
        void collect_completed();

    private:
        friend class UploadBatch;

        struct BatchResources {
            std::vector<std::shared_ptr<Buffer>> buffers;
            std::vector<std::shared_ptr<Image>> images;
        };

        struct PendingBatch {
            std::unique_ptr<CommandContext> context;
            BatchResources resources;
            GpuCompletionPoint completion;
        };

        [[nodiscard]] GpuCompletionPoint submit_batch(UploadBatch& batch);
        void abort_batch(UploadBatch& batch);
        void wait_for_pending_batches();

        Device& m_device;
        std::vector<PendingBatch> m_pending_batches;
        size_t m_open_batch_count = 0;
    };

    class COMET_API UploadBatch final {
    public:
        ~UploadBatch();

        UploadBatch(const UploadBatch&) = delete;
        UploadBatch& operator=(const UploadBatch&) = delete;
        UploadBatch(UploadBatch&&) noexcept = delete;
        UploadBatch& operator=(UploadBatch&&) noexcept = delete;

        void enqueue_upload(
            std::shared_ptr<Buffer> destination,
            std::span<const std::byte> data,
            const ResourceState& after,
            std::string_view debug_name = {});

        void enqueue_upload(
            std::shared_ptr<Image> destination,
            std::span<const std::byte> data,
            const ImageState& before,
            const ImageState& after,
            std::string_view debug_name = {});

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
