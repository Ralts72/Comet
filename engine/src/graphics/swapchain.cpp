#include "swapchain.h"
#include "graphics/creation.h"

#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "context.h"
#include "core/window.h"
#include "device.h"
#include "graphics/resource/image.h"
#include "graphics/result.h"
#include "graphics/synchronization/semaphore.h"

#include <utility>

namespace Comet {
    namespace {
        template<typename Value, typename Query>
        vk::Result enumerate_surface_values(std::vector<Value>& values, const Query& query) {
            constexpr uint32_t MAX_ATTEMPTS = 4;
            for(uint32_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
                uint32_t count = 0;
                auto result = query(&count, nullptr);
                if(result != vk::Result::eSuccess)
                    return result;
                values.resize(count);
                if(count == 0)
                    return vk::Result::eSuccess;
                result = query(&count, values.data());
                if(result == vk::Result::eSuccess) {
                    values.resize(count);
                    return result;
                }
                if(result != vk::Result::eIncomplete)
                    return result;
            }
            return vk::Result::eIncomplete;
        }

        GpuResourceResult<std::vector<vk::Image>> get_swapchain_images(
            const vk::Device device, const vk::SwapchainKHR swapchain) {
            constexpr uint32_t MAX_ATTEMPTS = 4;
            for(uint32_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
                uint32_t image_count = 0;
                vk::Result result = device.getSwapchainImagesKHR(swapchain, &image_count, nullptr);
                if(result != vk::Result::eSuccess) {
                    return GpuResourceResult<std::vector<vk::Image>>::failure(result);
                }
                if(image_count == 0) {
                    return GpuResourceResult<std::vector<vk::Image>>::failure(
                        vk::Result::eErrorUnknown);
                }

                std::vector<vk::Image> images(image_count);
                result = device.getSwapchainImagesKHR(swapchain, &image_count, images.data());
                if(result == vk::Result::eIncomplete) {
                    continue;
                }
                if(result != vk::Result::eSuccess) {
                    return GpuResourceResult<std::vector<vk::Image>>::failure(result);
                }
                images.resize(image_count);
                return GpuResourceResult<std::vector<vk::Image>>::success(std::move(images));
            }
            return GpuResourceResult<std::vector<vk::Image>>::failure(vk::Result::eIncomplete);
        }
    }

    Swapchain::Generation::Generation(vk::UniqueSwapchainKHR swapchain,
        std::vector<std::shared_ptr<Image>> images, SwapchainConfig config,
        std::shared_ptr<vk::UniqueSurfaceKHR> surface)
        : m_surface(std::move(surface)), m_swapchain(std::move(swapchain)),
          m_images(std::move(images)), m_config(std::move(config)) {}

    Swapchain::Generation::~Generation() {
        m_images.clear();
    }

    SwapchainCompatibility compare_swapchain_configs(
        const SwapchainConfig& previous, const SwapchainConfig& current) {
        return {.extent_changed = previous.extent != current.extent,
            .format_changed = previous.surface_format != current.surface_format,
            .image_count_changed = previous.image_count != current.image_count};
    }

    Swapchain::Swapchain(
        const Window& window, Context& context, Device& device, const SwapchainRequest& request)
        : m_window(window), m_context(context), m_device(device), m_request(request) {}

    Result<std::unique_ptr<Swapchain>, GraphicsError> Swapchain::create(
        const Window& window, Context& context, Device& device, const SwapchainRequest& request) {
        using CreationResult = Result<std::unique_ptr<Swapchain>, GraphicsError>;
        std::unique_ptr<Swapchain> candidate(new Swapchain(window, context, device, request));
        auto initialized = candidate->recreate();
        if(!initialized)
            return CreationResult::failure(initialized.error());
        if(initialized.value() == RecreateStatus::Deferred)
            return CreationResult::failure(
                {"Initial swapchain creation requires a drawable window"});
        return CreationResult::success(std::move(candidate));
    }

    Result<Swapchain::RecreateStatus, GraphicsError> Swapchain::recreate() {
        PROFILE_SCOPE("Swapchain::Recreate");
        const auto physical_device = m_context.get_physical_device();
        const auto surface = m_context.get_surface();
        vk::SurfaceCapabilitiesKHR capabilities;
        const auto queried = physical_device.getSurfaceCapabilitiesKHR(surface, &capabilities);
        if(queried != vk::Result::eSuccess)
            return Result<RecreateStatus, GraphicsError>::failure(
                {"Cannot query surface capabilities", queried});
        std::vector<vk::SurfaceFormatKHR> surface_formats;
        const auto formats = enumerate_surface_values(
            surface_formats, [&](uint32_t* count, vk::SurfaceFormatKHR* values) {
                return physical_device.getSurfaceFormatsKHR(surface, count, values);
            });
        if(formats != vk::Result::eSuccess)
            return Result<RecreateStatus, GraphicsError>::failure(
                {"Cannot query surface formats", formats});
        std::vector<vk::PresentModeKHR> present_modes;
        const auto modes = enumerate_surface_values(
            present_modes, [&](uint32_t* count, vk::PresentModeKHR* values) {
                return physical_device.getSurfacePresentModesKHR(surface, count, values);
            });
        if(modes != vk::Result::eSuccess)
            return Result<RecreateStatus, GraphicsError>::failure(
                {"Cannot query present modes", modes});
        const auto framebuffer_size = m_window.get_framebuffer_size();
        // 启动时选择一次；resize / surface 恢复不能悄悄改变输出编码。
        const auto selection = select_swapchain(capabilities, surface_formats, present_modes,
            vk::Extent2D{framebuffer_size.x, framebuffer_size.y}, m_request, m_output_format);
        const auto& [status, config, message] = selection;
        if(status == SwapchainStatus::Deferred)
            return Result<RecreateStatus, GraphicsError>::success(RecreateStatus::Deferred);
        if(status == SwapchainStatus::Unsupported)
            return Result<RecreateStatus, GraphicsError>::failure({message});
        if(!message.empty()) {
            LOG_WARN("Swapchain selection: {}", message);
        }
        auto candidate = try_create_generation(config);
        if(!candidate) {
            return Result<RecreateStatus, GraphicsError>::failure(candidate.error());
        }
        m_active_generation = std::move(candidate).value();
        if(!m_output_format) {
            const char* requested = "sdr";
            if(m_request.output_mode == OutputMode::Hdr)
                requested = "hdr";
            else if(m_request.output_mode == OutputMode::Auto)
                requested = "auto";
            const char* actual = "sdr";
            if(config.surface_format.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT)
                actual = "hdr (extended linear sRGB)";
            LOG_INFO("Display output: requested={}, actual={}", requested, actual);
            m_output_format = config.surface_format;
        }

        LOG_INFO(
            "Vulkan swapchain created: images={}, extent={}x{}, format={}, color_space={}, present_mode={}, transform={}, composite_alpha={}, usage={}, layers={}, clipped={}",
            get_images().size(), config.extent.width, config.extent.height,
            vk::to_string(config.surface_format.format),
            vk::to_string(config.surface_format.colorSpace), vk::to_string(config.present_mode),
            vk::to_string(config.transform), vk::to_string(config.composite_alpha),
            vk::to_string(config.usage), config.image_layers, config.clipped);
        return Result<RecreateStatus, GraphicsError>::success(RecreateStatus::Recreated);
    }

    Result<void, GraphicsError> Swapchain::recreate_surface() {
        m_active_generation.reset();
        return m_context.recreate_surface(m_window);
    }

    Swapchain::GenerationResult Swapchain::try_create_generation(const SwapchainConfig& config) {
        vk::SharingMode image_sharing_mode;
        std::vector<uint32_t> queue_family_indices;
        if(m_context.is_same_queue_families()) {
            image_sharing_mode = vk::SharingMode::eExclusive;
        } else {
            image_sharing_mode = vk::SharingMode::eConcurrent;
            queue_family_indices.push_back(
                m_context.get_graphics_queue_family().queue_family_index.value());
            queue_family_indices.push_back(
                m_context.get_present_queue_family().queue_family_index.value());
        }

        const vk::SwapchainKHR old_swapchain =
            m_active_generation ? m_active_generation->get() : vk::SwapchainKHR{};
        vk::SwapchainCreateInfoKHR create_info{};
        create_info.surface = m_context.get_surface();
        create_info.minImageCount = config.image_count;
        create_info.imageFormat = config.surface_format.format;
        create_info.imageColorSpace = config.surface_format.colorSpace;
        create_info.imageExtent = config.extent;
        create_info.imageArrayLayers = config.image_layers;
        create_info.imageUsage = config.usage;
        create_info.imageSharingMode = image_sharing_mode;
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
        create_info.pQueueFamilyIndices =
            queue_family_indices.empty() ? nullptr : queue_family_indices.data();
        create_info.preTransform = config.transform;
        create_info.compositeAlpha = config.composite_alpha;
        create_info.presentMode = config.present_mode;
        create_info.clipped = config.clipped ? VK_TRUE : VK_FALSE;
        create_info.oldSwapchain = old_swapchain;

        // oldSwapchain 一经用于创建即退休；失败也不能重新作为 active 发布。
        auto previous = std::move(m_active_generation);
        auto swapchain = Graphics::create_handle<vk::SwapchainKHR>(
            m_device.get(), "Cannot create swapchain", [&](vk::SwapchainKHR* output) noexcept {
                return m_device.get().createSwapchainKHR(&create_info, nullptr, output);
            });
        if(!swapchain)
            return GenerationResult::failure(swapchain.error());

        std::shared_ptr<Generation> generation(new Generation(
            std::move(swapchain).value(), {}, config, m_context.get_surface_owner()));
        auto images_attempt = get_swapchain_images(m_device.get(), generation->get());
        if(!images_attempt)
            return GenerationResult::failure(
                {"Cannot query swapchain images: " + images_attempt.error().message,
                    images_attempt.result()});

        const auto images = std::move(images_attempt).value();
        std::vector<std::shared_ptr<Image>> image_owners;
        image_owners.reserve(images.size());
        const ImageInfo image_info{.format = Graphics::vk_to_format(config.surface_format.format),
            .extent = Math::Vec3u(config.extent.width, config.extent.height, 1),
            .usage = Flags<ImageUsage>(ImageUsage::ColorAttachment)};
        for(const auto image : images) {
            image_owners.emplace_back(Image::wrap(m_device, image, image_info));
        }

        generation->m_images = std::move(image_owners);
        return GenerationResult::success(std::move(generation));
    }

    Result<std::optional<uint32_t>, GraphicsError> Swapchain::acquire_next_image(
        const Semaphore& semaphore) {
        using AcquisitionResult = Result<std::optional<uint32_t>, GraphicsError>;
        if(!m_active_generation)
            return AcquisitionResult::failure({"Cannot acquire from an inactive swapchain"});
        uint32_t image_index = 0;
        auto& generation = *m_active_generation;
        const auto result = m_device.get().acquireNextImageKHR(
            generation.get(), UINT64_MAX, semaphore.get(), VK_NULL_HANDLE, &image_index);
        if(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
            generation.m_current_index = image_index;
            return AcquisitionResult::success(image_index);
        }
        if(result == vk::Result::eErrorOutOfDateKHR)
            return AcquisitionResult::success(std::nullopt);
        return AcquisitionResult::failure(
            {"Cannot acquire swapchain image: " + vk::to_string(result), result});
    }

    Swapchain::Generation& Swapchain::active_generation() {
        if(!m_active_generation) {
            LOG_FATAL("Swapchain has no active generation");
        }
        return *m_active_generation;
    }

    const Swapchain::Generation& Swapchain::active_generation() const {
        if(!m_active_generation) {
            LOG_FATAL("Swapchain has no active generation");
        }
        return *m_active_generation;
    }

    uint32_t Swapchain::get_current_index() const {
        return active_generation().get_current_index();
    }

    const std::vector<std::shared_ptr<Image>>& Swapchain::get_images() const {
        return active_generation().get_images();
    }

    uint32_t Swapchain::get_width() const {
        return active_generation().get_config().extent.width;
    }

    uint32_t Swapchain::get_height() const {
        return active_generation().get_config().extent.height;
    }

    const vk::SwapchainKHR& Swapchain::get() const {
        return active_generation().get();
    }

    const std::shared_ptr<Swapchain::Generation>& Swapchain::get_active_generation() const {
        return m_active_generation;
    }
}
