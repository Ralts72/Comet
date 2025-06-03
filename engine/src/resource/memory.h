#pragma once

namespace Comet {
    template<typename T>
    class BlockMemoryAllocator {
    public:
        static BlockMemoryAllocator& getInstance() {
            static BlockMemoryAllocator instance;
            return instance;
        }

        BlockMemoryAllocator(size_t block_mem_count = 256);

        BlockMemoryAllocator(const BlockMemoryAllocator&) = delete;

        BlockMemoryAllocator& operator=(const BlockMemoryAllocator&) = delete;

        BlockMemoryAllocator(BlockMemoryAllocator&& o) noexcept;

        BlockMemoryAllocator& operator=(BlockMemoryAllocator&& o) noexcept;

        template<typename... Args>
        T* allocate(Args&&... args) noexcept;

        void deallocate(T* p) noexcept;

        size_t blockCount() const noexcept;

        size_t unuseCount(size_t block_index) const;

        size_t inuseCount(size_t block_index) const;

        void freeAll() noexcept;

        ~BlockMemoryAllocator() { freeAll(); }

    private:
        struct Mem {
            unsigned char m_mem[sizeof(T)];
            bool m_in_use = false;
        };

        template<typename U>
        struct Node {
            U m_data;
            Node* m_next{};
        };

        struct Block {
            Mem* m_mem{};
            Node<size_t>* m_unused_index_head{};
            Node<size_t>* m_in_use_index_head{};
            const size_t m_block_mem_count;

            ~Block() noexcept;
        };

        Node<Block>* m_block_head{};
        const size_t m_block_mem_count;

        Block* ensureBlock() noexcept;
    };
}
