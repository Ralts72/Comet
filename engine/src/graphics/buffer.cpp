#include "buffer.h"
#include "../resource/buffer_res.h"

namespace Comet {
    Buffer::Buffer(BufferRes* res):m_res(res) {}

    Buffer::Buffer(const Buffer& other):m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    Buffer::Buffer(Buffer&& other) noexcept:m_res(other.m_res) {
        other.m_res = nullptr;
    }

    Buffer& Buffer::operator=(const Buffer& other) noexcept {
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

    Buffer& Buffer::operator=(Buffer&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    Buffer::~Buffer() {
        release();
    }

    Buffer::MapState Buffer::getMapState() const {
        return m_res->getMapState();
    }

    uint64_t Buffer::getSize() const {
        return m_res->getSize();
    }

    void Buffer::unmap() {
        m_res->unmap();
    }

    void Buffer::mapAsync(uint64_t offset, uint64_t size) {
        m_res->mapAsync(offset, size);
    }

    void Buffer::mapAsync() {
        m_res->mapAsync();
    }

    void* Buffer::getMappedRange() {
        return m_res->getMappedRange();
    }

    void* Buffer::getMappedRange(uint64_t offset) {
        return m_res->getMappedRange(offset);
    }

    void Buffer::flush() {
        m_res->flush();
    }

    void Buffer::flush(uint64_t offset, uint64_t size) {
        m_res->flush(offset, size);
    }

    void Buffer::buffData(void* data, size_t size, size_t offset) {
        m_res->buffData(data, size, offset);
    }

    void Buffer::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }
}
