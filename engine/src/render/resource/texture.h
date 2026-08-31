#pragma once
#include "common/export.h"
#include "core/math_utils.h"
#include "graphics/synchronization/gpu_completion_point.h"
#include "render/resource/texture_data.h"

#include <cstddef>
#include <memory>
#include <span>

namespace Comet {
    class Image;
    class ImageView;
    class Device;
    class UploadManager;

    class COMET_API Texture {
    public:
        Texture(
            Device& device,
            UploadManager& upload_manager,
            const TextureData& data);
        Texture(
            Device& device,
            UploadManager& upload_manager,
            int width,
            int height,
            Math::Vec4u color);
        ~Texture();

        [[nodiscard]] int get_width() const { return m_width; }
        [[nodiscard]] int get_height() const { return m_height; }
        [[nodiscard]] std::shared_ptr<Image> get_image() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_image_view() const { return m_image_view; }
        [[nodiscard]] const GpuCompletionPoint& get_ready_completion() const {
            return m_ready_completion;
        }
    private:
        void create_image(
            Device& device,
            UploadManager& upload_manager,
            std::span<const std::byte> data);

        int m_width;
        int m_height;
        Format m_format;
        std::shared_ptr<ImageView> m_image_view;
        GpuCompletionPoint m_ready_completion;
    };
}
