#pragma once
#include "../common/flags.h"
#include "../common/enums.h"

namespace Comet {
    class BufferRes;

    class Buffer {
    public:
        struct Descriptor {
            uint64_t size;
            Flags<BufferUsage> usage;
            MemoryType memoryType;
        };

        enum class MapState {
            Unmapped,
            Mapped,
        };

        Buffer() = default;

        explicit Buffer(BufferRes*);

        Buffer(const Buffer&);

        Buffer(Buffer&&) noexcept;

        Buffer& operator=(const Buffer&) noexcept;

        Buffer& operator=(Buffer&&) noexcept;

        ~Buffer();

        const BufferRes& getRes() const noexcept { return *m_res; }
        BufferRes& getRes() noexcept { return *m_res; }

        MapState getMapState() const;

        uint64_t getSize() const;

        void unmap();

        void mapAsync(uint64_t offset, uint64_t size);

        void mapAsync();

        [[nodiscard]] void* getMappedRange();

        void* getMappedRange(uint64_t offset);

        void flush();

        void flush(uint64_t offset, uint64_t size);

        void buffData(void* data, size_t size, size_t offset);

        operator bool() const noexcept { return m_res; }

        void release();

    private:
        BufferRes* m_res{};
    };
}
