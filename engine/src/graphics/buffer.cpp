#include "buffer.h"

namespace Comet {
    Buffer::Buffer(BufferRes*) {}
    Buffer::Buffer(const Buffer&) {}
    Buffer::Buffer(Buffer&&) noexcept {}
    Buffer& Buffer::operator=(const Buffer&) noexcept {}
    Buffer& Buffer::operator=(Buffer&&) noexcept {}
    Buffer::~Buffer() {}
    Buffer::MapState Buffer::getMapState() const {}
    uint64_t Buffer::getSize() const {}
    void Buffer::unmap() {}
    void Buffer::mapAsync(uint64_t offset, uint64_t size) {}
    void Buffer::mapAsync() {}
    void* Buffer::getMappedRange() {}
    void* Buffer::getMappedRange(uint64_t offset) {}
    void Buffer::flush() {}
    void Buffer::flush(uint64_t offset, uint64_t size) {}
    void Buffer::buffData(void* data, size_t size, size_t offset) {}
    Buffer::operator bool() const noexcept {}
    void Buffer::release() {}
}
