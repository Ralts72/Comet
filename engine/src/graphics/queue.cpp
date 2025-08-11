#include "queue.h"

namespace Comet {
    Queue::Queue(uint32_t family_index, uint32_t index, vk::Queue queue, QueueType type) : m_family_index(family_index), m_index(index), m_queue(queue), m_type(type) {}
}
