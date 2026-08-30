#pragma once

#include "common/export.h"
#include "graphics/queue.h"
#include "graphics/synchronization/resource_state.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Comet {
    class Buffer;
    class CommandContext;
    class Device;
    class Image;

    class COMET_API UploadManager {
    public:
        explicit UploadManager(Device& device);
        ~UploadManager();

        UploadManager(const UploadManager&) = delete;
        UploadManager& operator=(const UploadManager&) = delete;
        UploadManager(UploadManager&&) noexcept = delete;
        UploadManager& operator=(UploadManager&&) noexcept = delete;

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

        [[nodiscard]] std::optional<GpuCompletionPoint> flush_batch();
        void upload_and_wait();
        void collect_completed();

    private:
        struct BatchResources {
            std::vector<std::shared_ptr<Buffer>> buffers;
            std::vector<std::shared_ptr<Image>> images;
        };

        struct PendingBatch {
            std::unique_ptr<CommandContext> context;
            BatchResources resources;
            GpuCompletionPoint completion;
        };

        [[nodiscard]] CommandContext& get_active_context();
        void wait_for_pending_batches();

        Device& m_device;
        std::unique_ptr<CommandContext> m_active_context;
        BatchResources m_active_resources;
        std::vector<PendingBatch> m_pending_batches;
    };
}
