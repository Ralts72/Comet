#pragma once
#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "render/resource/texture_data.h"

#include <memory>

namespace Comet {
    class ImageView;
    class Device;
    class UploadManager;

    class COMET_API Texture {
    public:
        [[nodiscard]] static std::shared_ptr<Texture> create(
            Device& device, UploadManager& upload_manager, const TextureData& data);
        [[nodiscard]] static GpuResourceResult<std::shared_ptr<Texture>> try_create(
            Device& device, UploadManager& upload_manager, const TextureData& data,
            bool within_budget);

        ~Texture() = default;

        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const {
            return m_image_view;
        }
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }

    private:
        Texture(int width, int height, std::shared_ptr<ImageView> image_view,
            GpuCompletionPoint ready_completion);

        int m_width;
        int m_height;
        std::shared_ptr<ImageView> m_image_view;
        GpuCompletionPoint m_ready_completion;
    };
}
