#include "memory.h"
#include <exception>
#include "../common/macro.h"

namespace Comet {
    template<typename T>
    BlockMemoryAllocator<T>::BlockMemoryAllocator(size_t block_mem_count): m_block_mem_count{block_mem_count} {}

    template<typename T>
    BlockMemoryAllocator<T>::BlockMemoryAllocator(BlockMemoryAllocator&& o) noexcept : m_block_head{o.m_block_head}, m_block_mem_count{o.m_block_mem_count} {
        o.m_block_mem_count = 0;
        o.m_block_head = nullptr;
    }

    template<typename T>
    BlockMemoryAllocator<T>& BlockMemoryAllocator<T>::operator=(BlockMemoryAllocator&& o) noexcept {
        if(&o != this) {
            m_block_mem_count = o.m_block_mem_count;
            m_block_head = o.m_block_head;
            o.m_block_mem_count = 0;
            o.m_block_head = nullptr;
        }
        return *this;
    }

    template<typename T>
    template<typename... Args>
    T* BlockMemoryAllocator<T>::allocate(Args&&... args) noexcept {
        Block* block = ensure_block();
        if(block == nullptr) {
            return nullptr;
        }

        Node<size_t>* index_node = block->m_unused_index_head;
        Mem* mem = block->m_mem + index_node->m_data;

        T* elem;
        try {
            elem =
                    std::construct_at(static_cast<T*>(&mem->m_mem), std::forward<Args>(args)...);
        } catch(...) {
            LOG_ERROR("catch exception when deconstruct object");
            return nullptr;
        }

        block->m_unused_index_head = block->m_unused_index_head->m_next;
        index_node->m_next = block->m_in_use_index_head;
        block->m_in_use_index_head = index_node;
        mem->m_in_use = true;

        return elem;
    }

    template<typename T>
    void BlockMemoryAllocator<T>::deallocate(T* p) noexcept {
        Node<Block>* block_node = m_block_head;

        while(block_node &&
              (static_cast<std::uintptr_t>(p) < static_cast<std::uintptr_t>(block_node->m_data.m_mem) ||
               static_cast<std::uintptr_t>(p) >=
               static_cast<std::uintptr_t>(block_node->m_data.m_mem +
                                           m_block_mem_count * sizeof(Mem)))) {
            block_node = block_node->m_next;
        }

        if(block_node) {
            ASSERT(
                (static_cast<std::ptrdiff_t>(p) - static_cast<std::ptrdiff_t>(block_node->m_data.m_mem)) %
                sizeof(Mem) ==
                0,
                "invalid memory address");

            size_t idx = static_cast<Mem*>(p) - block_node->m_data.m_mem;
            Mem* mem = block_node->m_data.m_mem + idx;

            if(!mem->m_in_use) {
                LOG_ERROR("memory is not in use when deallocate");
                return;
            }

            try {
                static_cast<T*>(&mem->m_mem)->~T();
            } catch(...) {
                LOG_ERROR("catch exception when deconstruct object");
                return;
            }

            mem->m_in_use = false;
            Node<size_t>* in_use_node = block_node->m_data.m_in_use_index_head;
            block_node->m_data.m_in_use_index_head = in_use_node->m_next;
            in_use_node->m_data = idx;

            Block& block = block_node->m_data;
            in_use_node->m_next = block.m_unused_index_head;
            block.m_unused_index_head = in_use_node;
            return;
        }

        LOG_ERROR("object is not in pool");
        delete p;
    }

    template<typename T>
    size_t BlockMemoryAllocator<T>::blockCount() const noexcept {
        size_t count = 0;
        Node<Block>* block = m_block_head;
        while(block) {
            count++;
            block = block->m_next;
        }
        return count;
    }


    template<typename T>
    size_t BlockMemoryAllocator<T>::unuseCount(size_t block_index) const {
        Node<Block>* block = m_block_head;
        while(block_index > 0 && block) {
            block = block->m_next;
            --block_index;
        }
        if(block) {
            size_t count = 0;
            Node<size_t>* node = block->m_data.m_unused_index_head;
            while(node) {
                node = node->m_next;
                count++;
            }
            return count;
        }
        return 0;
    }


    template<typename T>
    size_t BlockMemoryAllocator<T>::inuseCount(size_t block_index) const {
        Node<Block>* block = m_block_head;
        while(block_index > 0 && block) {
            block = block->m_next;
            --block_index;
        }
        if(block) {
            size_t count = 0;
            Node<size_t>* node = block->m_data.m_in_use_index_head;
            while(node) {
                node = node->m_next;
                count++;
            }
            return count;
        }
        return 0;
    }

    template<typename T>
    void BlockMemoryAllocator<T>::freeAll() noexcept {
        if(!m_block_head) {
            return;
        }
        Node<Block>* node = m_block_head;
        while(node) {
            Node<Block>* cur = node;
            node = node->m_next;
            delete cur;
        }
        m_block_head = nullptr;
    }


    template<typename T>
    BlockMemoryAllocator<T>::Block::~Block() noexcept {
        for(size_t i = 0; i < m_block_mem_count; i++) {
            Mem* mem = m_mem + i;
            if(mem->m_in_use) {
                try {
                    static_cast<T*>(mem)->~T();
                } catch(...) {
                    LOG_ERROR("caught exception when deconstruct object");
                }
            }
        } {
            Node<size_t>* node = m_in_use_index_head;
            while(node) {
                Node<size_t>* cur = node;
                node = node->m_next;
                try {
                    delete cur;
                } catch(...) {
                    LOG_ERROR("caught exception when object deconstruct");
                }
            }
        } {
            Node<size_t>* node = m_unused_index_head;
            while(node) {
                Node<size_t>* cur = node;
                node = node->m_next;
                try {
                    delete cur;
                } catch(...) {
                    LOG_ERROR("caught exception when object deconstruct");
                }
            }
        }
        delete[] m_mem;
    }

    template<typename T>
    typename BlockMemoryAllocator<T>::Block* BlockMemoryAllocator<T>::ensure_block() noexcept {
        Node<Block>* node = m_block_head;
        Node<Block>* prev = m_block_head;
        while(node && !node->m_data.m_unused_index_head) {
            prev = node;
            node = node->m_next;
        }
        if(!node) {
            Node<Block>* new_block = new(std::nothrow) Node<Block>{nullptr, nullptr, nullptr, m_block_mem_count};
            if(!new_block) {
                LOG_ERROR("bad alloc exception! out of memory");
                return nullptr;
            }
            new_block->m_data.m_mem = new(std::nothrow) Mem[m_block_mem_count];
            if(!new_block->m_data.m_mem) {
                LOG_ERROR("allocate memory block failed");
                return nullptr;
            }
            Node<size_t>* prev_index = new_block->m_data.m_unused_index_head;
            for(size_t i = 0; i < m_block_mem_count; i++) {
                Node<size_t>* new_index = new(std::nothrow) Node<size_t>;
                if(!new_index) {
                    LOG_ERROR("bad alloc exception! out of memory");
                    continue;
                }
                new_index->m_data = i;
                if(prev_index) {
                    prev_index->m_next = new_index;
                } else {
                    new_block->m_data.m_unused_index_head = new_index;
                }
                prev_index = new_index;
            }
            if(prev) {
                prev->m_next = new_block;
            } else {
                m_block_head = new_block;
            }
            return &new_block->m_data;
        }
        return &node->m_data;
    }
}

