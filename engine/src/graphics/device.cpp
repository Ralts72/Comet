#include "device.h"
#include "context.h"
#include "fence.h"
#include "queue.h"
#include "core/logger/logger.h"
#include "command_buffer.h"
#include "common/profiler.h"

namespace Comet {
    static std::vector<DeviceFeature> s_required_extensions = {
#ifdef __APPLE__
        {"VK_KHR_portability_subset", true},
#endif
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME, true},
        {"VK_KHR_buffer_device_address", true}
    };

    Device::Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count, const VkSettings& settings)
        : m_context(context), m_settings(settings) {
        PROFILE_SCOPE("Device::Constructor");
        if(!context) {
            LOG_ERROR("Must create a vulkan graphics context before create device");
            return;
        }
        auto [graphics_queue_family_index, graphics_queue_counts] = context->get_graphics_queue_family();
        auto [present_queue_family_index, present_queue_counts] = context->get_present_queue_family();
        if(graphics_queue_count > graphics_queue_counts) {
            LOG_ERROR("Graphic queue count {} is larger than available queue count {}", graphics_queue_count, graphics_queue_counts);
            return;
        }
        if(present_queue_count > present_queue_counts) {
            LOG_ERROR("Present queue count {} is larger than available queue count {}", present_queue_count, present_queue_counts);
            return;
        }
        std::vector<float> graphics_queue_priorities(graphics_queue_count, 0.0f);
        std::vector<float> present_queue_priorities(present_queue_count, 1.0f);
        std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
        uint32_t queue_count = graphics_queue_count;
        const bool is_same_queue_family = context->is_same_queue_families();
        if(is_same_queue_family) {
            queue_count += present_queue_count;
            graphics_queue_priorities.insert(graphics_queue_priorities.end(), present_queue_priorities.begin(), present_queue_priorities.end());
            if(queue_count > graphics_queue_counts) {
                queue_count = graphics_queue_counts;
                graphics_queue_priorities.resize(queue_count);
            }
        }
        vk::DeviceQueueCreateInfo queue_create_info = {};
        queue_create_info.queueFamilyIndex = graphics_queue_family_index.value();
        queue_create_info.queueCount = is_same_queue_family ? queue_count : graphics_queue_count;
        queue_create_info.pQueuePriorities = graphics_queue_priorities.data();
        queue_create_infos.push_back(queue_create_info);

        if(!is_same_queue_family) {
            vk::DeviceQueueCreateInfo present_queue_create_info = {};
            present_queue_create_info.queueFamilyIndex = present_queue_family_index.value();
            present_queue_create_info.queueCount = present_queue_count;
            present_queue_create_info.pQueuePriorities = present_queue_priorities.data();
            queue_create_infos.push_back(present_queue_create_info);
        }

        std::set<std::string> available_extensions;
        for(const auto& prop: context->get_physical_device().enumerateDeviceExtensionProperties()) {
            available_extensions.emplace(prop.extensionName);
        }
        const std::vector<const char*> enabled_extensions = build_enabled_list(s_required_extensions, available_extensions, "device extension");
        auto features = context->get_physical_device().getFeatures();

        vk::DeviceCreateInfo create_info = {};
        create_info.queueCreateInfoCount = queue_create_infos.size();
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        create_info.enabledExtensionCount = enabled_extensions.size();
        create_info.pEnabledFeatures = &features;
        m_device = context->get_physical_device().createDevice(create_info);
        LOG_INFO("Vulkan logical device created successfully");

        for(uint32_t i = 0; i < graphics_queue_count; ++i) {
            auto vk_queue = m_device.getQueue(graphics_queue_family_index.value(), i);
            auto queue = std::make_shared<Queue>(graphics_queue_family_index.value(), i, vk_queue, QueueType::Graphics);
            m_graphics_queues.emplace_back(queue);
        }
        for(uint32_t i = 0; i < present_queue_count; ++i) {
            auto vk_queue = m_device.getQueue(present_queue_family_index.value(), i);
            auto queue = std::make_shared<Queue>(present_queue_family_index.value(), i, vk_queue, QueueType::Present);
            m_present_queues.emplace_back(queue);
        }

        create_pipeline_cache();
        create_default_command_pool();
    }

    Device::~Device() {
        m_device.waitIdle();
        m_default_command_pool.reset();
        m_device.destroyPipelineCache(m_pipeline_cache);
        m_device.destroy();
    }

    void Device::create_default_command_pool() {
        m_default_command_pool = std::make_shared<CommandPool>(this, m_context->get_graphics_queue_family().queue_family_index.value());
    }

    void Device::wait_for_fences(const std::span<const Fence> fences, const bool wait_all, const uint64_t timeout) const {
        std::vector<vk::Fence> vk_fences;
        for(const auto& fence : fences) {
            vk_fences.push_back(fence.get_fence());
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
        for(const auto& fence : fences) {
            vk_fences.push_back(fence.get_fence());
        }
        m_device.resetFences(vk_fences);
    }

    void Device::wait_idle() {
        m_device.waitIdle();
    }
    uint32_t Device::get_memory_index(const vk::MemoryPropertyFlags mem_props, uint32_t memory_type_bits) const {
        const vk::PhysicalDeviceMemoryProperties physical_device_mem_props = m_context->get_memory_properties();
        if(physical_device_mem_props.memoryTypeCount == 0){
            LOG_FATAL("Physical device memory type count is 0");
        }
        for(uint32_t i = 0; i < physical_device_mem_props.memoryTypeCount; i++){
            const bool is_type_used = (memory_type_bits & (1 << i)) != 0;
            const bool has_required_props = (physical_device_mem_props.memoryTypes[i].propertyFlags & mem_props) == mem_props;
            if( is_type_used && has_required_props ){
                return i;
            }
        }
        LOG_ERROR("Can not find memory type index: type bit: {}", memory_type_bits);
        return 0;
    }

    void Device::create_pipeline_cache() {
        constexpr vk::PipelineCacheCreateInfo pcache_create_info = {};
        m_pipeline_cache = m_device.createPipelineCache(pcache_create_info);
        LOG_INFO("Vulkan pipeline cache created successfully");
    }
}
