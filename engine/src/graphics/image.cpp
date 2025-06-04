#include "image.h"
#include "../resource/image_res.h"

namespace Comet {
    ImageView::ImageView(ImageViewRes* res): m_res(res) {}

    ImageView::ImageView(const ImageView& other): m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    ImageView::ImageView(ImageView&& other) noexcept: m_res(other.m_res) {
        other.m_res = nullptr;
    }

    ImageView& ImageView::operator=(const ImageView& other) noexcept {
        if(&other != this) {
            if(m_res) {
                m_res->decrease();
            }
            m_res = other.m_res;
            if(m_res) {
                m_res->increase();
            }
        }
        return *this;
    }

    ImageView& ImageView::operator=(ImageView&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    ImageView::~ImageView() {
        release();
    }

    Image ImageView::getImage() const {
        return m_res->getImage();
    }

    void ImageView::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }

    Image::Image(ImageRes* res): m_res(res) {}

    Image::Image(const Image& other): m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    Image::Image(Image&& other) noexcept: m_res(other.m_res) {
        other.m_res = nullptr;
    }

    Image& Image::operator=(const Image& other) noexcept {
        if(&other != this) {
            if(m_res) {
                m_res->decrease();
            }
            m_res = other.m_res;
            if(m_res) {
                m_res->increase();
            }
        }
        return *this;
    }

    Image& Image::operator=(Image&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    ImageView Image::createView(const ImageView::Descriptor& desc) const {
        return m_res->createView(*this, desc);
    }

    Image::~Image() {
        release();
    }

    void Image::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }
}
