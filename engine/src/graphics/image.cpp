#include "image.h"

namespace Comet {
    ImageView::ImageView(ImageViewRes*) {}
    ImageView::ImageView(const ImageView&) {}
    ImageView::ImageView(ImageView&&) noexcept {}
    ImageView& ImageView::operator=(const ImageView&) noexcept {}
    ImageView& ImageView::operator=(ImageView&&) noexcept {}
    ImageView::~ImageView() {}
    Image ImageView::getImage() const {}
    void ImageView::release() {}
    Image::Image(ImageRes*) {}
    Image::Image(const Image&) {}
    Image::Image(Image&&) noexcept {}
    Image& Image::operator=(const Image&) noexcept {}
    Image& Image::operator=(Image&&) noexcept {}
    ImageView Image::createView(const ImageView::Descriptor&) {}
    Image::~Image() {}
    void Image::release() {}
}
