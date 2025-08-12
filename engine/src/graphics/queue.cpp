#include "queue.h"

namespace Comet {
    Queue::Queue(const uint32_t family_index, const uint32_t index, const vk::Queue queue, const QueueType type)
    : m_family_index(family_index), m_index(index), m_queue(queue), m_type(type) {}
}
