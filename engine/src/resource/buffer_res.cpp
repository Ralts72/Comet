#include "buffer_res.h"
#include "../graphics/device.h"
#include "../common/enum_convert.h"
#include  "memory_res.h"

namespace Comet {
    BufferRes::BufferRes(Device& device, vk::PhysicalDevice physicalDevice, const Buffer::Descriptor& desc): m_device(device) {
        std::vector<uint32_t> indices;
        if(device.getQueueFamilyIndices().hasSeparateQueue()) {
            indices.emplace_back(device.getQueueFamilyIndices().graphicsIndex.value());
            indices.emplace_back(device.getQueueFamilyIndices().presentIndex.value());
        } else {
            indices.emplace_back(device.getQueueFamilyIndices().graphicsIndex.value());
        }

        m_size = desc.size;
        createBuffer(device, desc);
        allocateMemory(
            device, physicalDevice,
            getMemoryProperty(physicalDevice, desc));
        m_device.getVkDevice().bindBufferMemory(m_buffer, m_memory->getVkMemory(), 0);
    }

    BufferRes::~BufferRes() {
        if(m_map_state != Buffer::MapState::Unmapped) {
            unmap();
        }
        delete m_memory;
        m_device.getVkDevice().destroyBuffer(m_buffer);
    }

    void BufferRes::unmap() {
        flush();
        m_device.getVkDevice().unmapMemory(m_memory->getVkMemory());
        m_map_state = Buffer::MapState::Unmapped;
        m_map = nullptr;
    }

    void BufferRes::mapAsync(uint64_t offset, uint64_t size) {
        m_map = m_device.getVkDevice().mapMemory(m_memory->getVkMemory(), offset, size);
        if(m_map) {
            m_mapped_offset = offset;
            m_mapped_size = size;
            m_map_state = Buffer::MapState::Mapped;
        }
    }

    void BufferRes::mapAsync() {
        m_map = m_device.getVkDevice().mapMemory(m_memory->getVkMemory(), 0, m_size);
        if(m_map) {
            m_mapped_offset = 0;
            m_mapped_size = m_size;
            m_map_state = Buffer::MapState::Mapped;
        }
    }

    void* BufferRes::getMappedRange(uint64_t offset) const {
        return static_cast<char*>(m_map) + offset;
    }

    void BufferRes::flush() const {
        if(!m_is_mapping_coherence) {
            vk::MappedMemoryRange range{};
            range.memory = m_memory->getVkMemory();
            range.offset = m_mapped_offset;
            range.size = m_mapped_size;
            m_device.getVkDevice().flushMappedMemoryRanges(range);
        }
    }

    void BufferRes::flush(uint64_t offset, uint64_t size) const {
        vk::MappedMemoryRange range{};
        range.memory = m_memory->getVkMemory();
        range.offset = offset;
        range.size = size;
        m_device.getVkDevice().flushMappedMemoryRanges(range);
    }

    void BufferRes::decrease() {
        Refcount::decrease();
        if(getCount() == 0) {
            //TODO
        }
    }

    void BufferRes::buffData(void* data, size_t size, size_t offset) {
        // Buffer::Descriptor desc;
        // desc.m_memory_type = MemoryType::Coherence;
        // desc.m_size = size;
        // desc.m_usage = BufferUsage::CopySrc;
        // Buffer buffer = m_device.CreateBuffer(desc);
        // buffer.MapAsync(0, size);
        // void* map = buffer.GetMappedRange();
        // memcpy(map, data, size);
        // buffer.Unmap();
        //
        // CommandEncoder encoder = m_device.CreateCommandEncoder();
        // CopyEncoder copy_encoder = encoder.BeginCopy();
        // copy_encoder.copyBufferToBuffer(buffer.Impl(), 0, *this, offset, size);
        // copy_encoder.End();
        // Command cmd = encoder.Finish();
        //
        // m_device.Submit(cmd);
    }

    vk::MemoryPropertyFlags BufferRes::getMemoryProperty(vk::PhysicalDevice phyDevice, const Buffer::Descriptor& desc) {
        vk::MemoryPropertyFlags mem_props{};
        switch(desc.memoryType) {
            case MemoryType::CPULocal:
                mem_props = vk::MemoryPropertyFlagBits::eHostVisible;
                break;
            case MemoryType::Coherence: {
                const auto props = phyDevice.getProperties();
                if(props.limits.nonCoherentAtomSize == 0) {
                    LOG_WARN("your GPU don't support coherence memory type");
                    mem_props = vk::MemoryPropertyFlagBits::eHostVisible;
                    m_is_mapping_coherence = false;
                } else {
                    mem_props = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
                    m_is_mapping_coherence = true;
                }
            }
            break;
            case MemoryType::GPULocal:
                mem_props = vk::MemoryPropertyFlagBits::eDeviceLocal;
                break;
        }
        return mem_props;
    }

    void BufferRes::createBuffer(Device& device, const Buffer::Descriptor& desc) {
        std::vector<uint32_t> indices;
        if(device.getQueueFamilyIndices().graphicsIndex == device.getQueueFamilyIndices().presentIndex) {
            indices.push_back(device.getQueueFamilyIndices().graphicsIndex.value());
        } else {
            indices.push_back(device.getQueueFamilyIndices().graphicsIndex.value());
            indices.push_back(device.getQueueFamilyIndices().presentIndex.value());
        }
        vk::BufferCreateInfo ci{};
        ci.size = desc.size;
        ci.usage = vk::BufferUsageFlagBits{static_cast<uint32_t>(bufferUsage2VK(desc.usage))};
        ci.queueFamilyIndexCount = indices.size();
        ci.pQueueFamilyIndices = indices.data();
        if(indices.size() > 1) {
            ci.sharingMode = vk::SharingMode::eConcurrent;
        } else {
            ci.sharingMode = vk::SharingMode::eExclusive;
        }
        m_buffer = m_device.getVkDevice().createBuffer(ci);
    }

    void BufferRes::allocateMemory(Device& device, vk::PhysicalDevice phyDevice, vk::MemoryPropertyFlags flags) {
        const auto requirements = m_device.getVkDevice().getBufferMemoryRequirements(m_buffer);

        if(const auto type = findMemoryType(phyDevice, requirements, flags); !type) {
            LOG_ERROR("find corresponding memory type failed");
        } else {
            m_memory = new(std::nothrow) MemoryRes{
                m_device, static_cast<uint64_t>(requirements.size), type.value()
            };
        }
    }
}
