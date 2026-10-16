#include "render/image_denoiser.h"
#include <OpenImageDenoise/oidn.hpp>

#include <array>
#include <cmath>
#include <cstring>

namespace CometEditor {
    using namespace Comet;
    struct ImageDenoiser::State {
        oidn::DeviceRef device;
        std::array<oidn::BufferRef, 4> buffers;
        std::array<oidn::FilterRef, 3> filters;
        int width = 0, height = 0;
    };
    ImageDenoiser::ImageDenoiser() : m_state(std::make_unique<State>()) {}
    ImageDenoiser::~ImageDenoiser() = default;

    Result<TextureData> ImageDenoiser::filter(
        const TextureData& color, const TextureData& albedo, const TextureData& normal) {
        const std::array<const TextureData*, 3> inputs{&color, &albedo, &normal};
        if(color.width <= 0 || color.height <= 0 || color.width > 1024 || color.height > 1024)
            return Result<TextureData>::failure("降噪图像尺寸须为 1～1024 像素");
        const auto bytes = std::size_t(color.width) * color.height * sizeof(float) * 4;
        for(std::size_t image = 0; image < inputs.size(); ++image) {
            const auto& input = *inputs[image];
            if(input.width != color.width || input.height != color.height || input.cubemap
                || input.mip_levels != 1 || input.format != Format::R32G32B32A32_SFLOAT
                || input.pixels.size() != bytes)
                return Result<TextureData>::failure("降噪需要同尺寸的 RGBA32F 颜色、反照率和法线");
            for(std::size_t offset = 0; offset < bytes; offset += sizeof(float)) {
                float value;
                std::memcpy(&value, input.pixels.data() + offset, sizeof(value));
                const auto channel = offset / sizeof(float) % 4;
                if(!std::isfinite(value)
                    || (channel < 3
                        && ((image < 2 && value < 0) || (image == 1 && value > 1)
                            || (image == 2 && std::abs(value) > 1))))
                    return Result<TextureData>::failure("降噪输入含非有限值或越界的辅助特征");
            }
        }
        auto& state = *m_state;
        if(!state.device) {
            state.device = oidn::newDevice();
            if(!state.device)
                return Result<TextureData>::failure("没有可用的 OIDN 降噪设备");
            state.device.commit();
        }
        const auto error = [&]() -> std::string {
            const char* message = nullptr;
            if(state.device.getError(message) != oidn::Error::None)
                return message ? message : "OIDN 降噪失败";
            return {};
        };
        if(auto message = error(); !message.empty())
            return Result<TextureData>::failure(std::move(message));
        if(state.width != color.width || state.height != color.height) {
            state.filters = {};
            for(auto& buffer : state.buffers)
                buffer = state.device.newBuffer(bytes);
            if(auto message = error(); !message.empty())
                return Result<TextureData>::failure(std::move(message));
            constexpr std::array names{"albedo", "normal"};
            for(std::size_t i = 0; i < 2; ++i) {
                auto& filter = state.filters[i];
                filter = state.device.newFilter("RT");
                filter.setImage(names[i], state.buffers[i + 1], oidn::Format::Float3, color.width,
                    color.height, 0, 4 * sizeof(float));
                filter.setImage("output", state.buffers[i + 1], oidn::Format::Float3, color.width,
                    color.height, 0, 4 * sizeof(float));
                filter.commit();
            }
            auto& filter = state.filters[2];
            filter = state.device.newFilter("RT");
            constexpr std::array beauty_names{"color", "albedo", "normal", "output"};
            for(std::size_t i = 0; i < state.buffers.size(); ++i)
                filter.setImage(beauty_names[i], state.buffers[i], oidn::Format::Float3,
                    color.width, color.height, 0, 4 * sizeof(float));
            filter.set("hdr", true);
            filter.set("cleanAux", true);
            filter.commit();
            if(auto message = error(); !message.empty())
                return Result<TextureData>::failure(std::move(message));
            state.width = color.width;
            state.height = color.height;
        }
        for(std::size_t i = 0; i < inputs.size(); ++i)
            state.buffers[i].write(0, bytes, inputs[i]->pixels.data());
        for(auto& filter : state.filters)
            filter.execute();
        if(auto message = error(); !message.empty())
            return Result<TextureData>::failure(std::move(message));
        auto output = color;
        state.buffers[3].read(0, bytes, output.pixels.data());
        if(auto message = error(); !message.empty())
            return Result<TextureData>::failure(std::move(message));
        // OIDN writes Float3 with an RGBA stride; preserve the original coverage channel.
        for(std::size_t offset = 3 * sizeof(float); offset < bytes; offset += 4 * sizeof(float))
            std::memcpy(output.pixels.data() + offset, color.pixels.data() + offset, sizeof(float));
        return Result<TextureData>::success(std::move(output));
    }
}
