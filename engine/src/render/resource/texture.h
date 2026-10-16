#pragma once
#include "asset/content_fwd.h"
#include "graphics/result.h"
#include "graphics/synchronization/gpu_completion_point.h"

#include <memory>

namespace Comet {
    class ImageView;
    class Sampler;
    class Device;
    class UploadManager;
    struct TextureData;

    class COMET_API Texture {
    public:
        [[nodiscard]] static GpuResourceResult<std::shared_ptr<Texture>> try_create(Device& device,
            UploadManager& upload_manager, const TextureData& data, bool within_budget);

        // 已由渲染图创建并同步的图像；调用方负责首次使用前的布局与写入。
        [[nodiscard]] static std::shared_ptr<Texture> from_image(
            std::shared_ptr<ImageView> image, std::shared_ptr<Sampler> sampler);

        ~Texture() = default;

        [[nodiscard]] const std::shared_ptr<Sampler>& get_sampler() const { return m_sampler; }
        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const { return m_image_view; }
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }

    private:
        Texture(int width, int height, std::shared_ptr<ImageView> image_view,
            GpuCompletionPoint ready_completion, std::shared_ptr<Sampler> sampler);

        int m_width;
        int m_height;
        std::shared_ptr<ImageView> m_image_view;
        GpuCompletionPoint m_ready_completion;
        std::shared_ptr<Sampler> m_sampler;
    };
}
