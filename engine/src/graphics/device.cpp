#include "device.h"
#include "context.h"
#include "fence.h"
#include "queue.h"
#include "common/logger.h"
#include "command_buffer.h"
#include "command_context.h"
#include "common/profiler.h"
#include "allocator.h"

namespace Comet {
    Device::Device(Context& context)
        : Device(context, CreateInfo{}) {}

    Device::Device(Context& context, const CreateInfo create_info)
        : m_context(context) {
        PROFILE_SCOPE("Device::Constructor");
        auto [graphics_queue_family_index, graphics_queue_counts] = context.get_graphics_queue_family();
        auto [present_queue_family_index, present_queue_counts] = context.get_present_queue_family();
        if(create_info.graphics_queue_count > graphics_queue_counts) {
            LOG_FATAL("Requested graphics queue count {} exceeds available count {}",
                create_info.graphics_queue_count, graphics_queue_counts);
        }
        if(create_info.present_queue_count > present_queue_counts) {
            LOG_FATAL("Requested present queue count {} exceeds available count {}",
                create_info.present_queue_count, present_queue_counts);
        }
        std::vector<float> graphics_queue_priorities(create_info.graphics_queue_count, 0.0f);
        std::vector<float> present_queue_priorities(create_info.present_queue_count, 1.0f);
        std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
        uint32_t queue_count = create_info.graphics_queue_count;
        const bool is_same_queue_family = context.is_same_queue_families();
        if(is_same_queue_family) {
            queue_count += create_info.present_queue_count;
            graphics_queue_priorities.insert(graphics_queue_priorities.end(), present_queue_priorities.begin(), present_queue_priorities.end());
            if(queue_count > graphics_queue_counts) {
                queue_count = graphics_queue_counts;
                graphics_queue_priorities.resize(queue_count);
            }
        }
        vk::DeviceQueueCreateInfo queue_create_info = {};
        queue_create_info.queueFamilyIndex = graphics_queue_family_index.value();
        queue_create_info.queueCount = is_same_queue_family
            ? queue_count
            : create_info.graphics_queue_count;
        queue_create_info.pQueuePriorities = graphics_queue_priorities.data();
        queue_create_infos.push_back(queue_create_info);

        if(!is_same_queue_family) {
            vk::DeviceQueueCreateInfo present_queue_create_info = {};
            present_queue_create_info.queueFamilyIndex = present_queue_family_index.value();
            present_queue_create_info.queueCount = create_info.present_queue_count;
            present_queue_create_info.pQueuePriorities = present_queue_priorities.data();
            queue_create_infos.push_back(present_queue_create_info);
        }

        const auto physical_device = context.get_physical_device();
        m_capability = context.get_device_capability();

        vk::DeviceCreateInfo device_create_info = {};
        device_create_info.queueCreateInfoCount = queue_create_infos.size();
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.ppEnabledExtensionNames = m_capability.enabled_extensions.data();
        device_create_info.enabledExtensionCount = m_capability.enabled_extensions.size();
        device_create_info.pEnabledFeatures = &m_capability.enabled_features;
        m_device = physical_device.createDevice(device_create_info);
        LOG_INFO("Vulkan logical device created successfully");
        create_allocator();

        for(uint32_t i = 0; i < create_info.graphics_queue_count; ++i) {
            auto vk_queue = m_device.getQueue(graphics_queue_family_index.value(), i);
            m_graphics_queues.emplace_back(graphics_queue_family_index.value(), i, vk_queue, Queue::Type::Graphics);
        }
        for(uint32_t i = 0; i < create_info.present_queue_count; ++i) {
            auto vk_queue = m_device.getQueue(present_queue_family_index.value(), i);
            m_present_queues.emplace_back(present_queue_family_index.value(), i, vk_queue, Queue::Type::Present);
        }

        create_pipeline_cache();
        create_default_command_pool();
    }

    Device::~Device() {
        if(m_device) {
            m_device.waitIdle();
        }
        m_default_command_pool.reset();
        if(m_pipeline_cache) {
            m_device.destroyPipelineCache(m_pipeline_cache);
        }
        m_allocator.reset();
        if(m_device) {
            m_device.destroy();
        }
    }

    void Device::create_allocator() {
        Allocator::CreateInfo allocator_info = {};
        allocator_info.instance = m_context.instance();
        allocator_info.physical_device = m_context.get_physical_device();
        allocator_info.device = m_device;
        allocator_info.vulkan_api_version = VK_API_VERSION_1_3;

        m_allocator = std::make_unique<Allocator>(allocator_info);
    }

    void Device::create_default_command_pool() {
        m_default_command_pool = std::make_unique<CommandPool>(
            *this, m_context.get_graphics_queue_family().queue_family_index.value());
    }

    void Device::wait_for_fences(const std::span<const Fence> fences, const bool wait_all, const uint64_t timeout) const {
        std::vector<vk::Fence> vk_fences;
        for(const auto& fence: fences) {
            vk_fences.push_back(fence.get());
        }
        const auto result = m_device.waitForFences(vk_fences, wait_all, timeout);
        if(result != vk::Result::eSuccess) {
            LOG_ERROR("Failed to wait for fences: {}", vk::to_string(result));
        } else {
            LOG_DEBUG("Waited for fences successfully");
        }
    }

    void Device::reset_fences(const std::span<const Fence> fences) const {
        std::vector<vk::Fence> vk_fences;
        for(const auto& fence: fences) {
            vk_fences.push_back(fence.get());
        }
        m_device.resetFences(vk_fences);
    }

    void Device::wait_idle() const {
        m_device.waitIdle();
    }

    std::unique_ptr<CommandContext> Device::create_command_context() {
        return std::make_unique<CommandContext>(*this);
    }

    Allocator& Device::get_allocator() const {
        if(!m_allocator) {
            LOG_FATAL("Vulkan allocator is not initialized");
        }

        return *m_allocator;
    }

    void Device::create_pipeline_cache() {
        constexpr vk::PipelineCacheCreateInfo pcache_create_info = {};
        m_pipeline_cache = m_device.createPipelineCache(pcache_create_info);
        LOG_INFO("Vulkan pipeline cache created successfully");
    }
}
