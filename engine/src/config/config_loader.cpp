#include "config/config_loader.h"
#include "common/file_io.h"
#include <array>
#include <cmath>
#include <sstream>
#include <string_view>
#include <yaml-cpp/yaml.h>

namespace Comet {
    namespace {
        constexpr std::array SURFACE_FORMATS = {std::pair{"bgra8_srgb", Format::B8G8R8A8_SRGB},
            std::pair{"bgra8_unorm", Format::B8G8R8A8_UNORM},
            std::pair{"rgba8_srgb", Format::R8G8B8A8_SRGB},
            std::pair{"rgba8_unorm", Format::R8G8B8A8_UNORM}};
        constexpr std::array COLOR_SPACES = {
            std::pair{"srgb_nonlinear", ImageColorSpace::SrgbNonlinearKHR}};
        constexpr std::array OUTPUT_MODES = {std::pair{"sdr", OutputMode::Sdr},
            std::pair{"hdr", OutputMode::Hdr}, std::pair{"auto", OutputMode::Auto}};
        constexpr std::array DEPTH_FORMATS = {std::pair{"d32_float", Format::D32_SFLOAT},
            std::pair{"d24_unorm_s8_uint", Format::D24_UNORM_S8_UINT},
            std::pair{"d32_float_s8_uint", Format::D32_SFLOAT_S8_UINT}};
        constexpr std::array PRESENT_MODES = {std::pair{"immediate", PresentMode::Immediate},
            std::pair{"mailbox", PresentMode::Mailbox}, std::pair{"fifo", PresentMode::Fifo},
            std::pair{"fifo_relaxed", PresentMode::FifoRelaxed}};
        std::string config_error(
            std::string_view path, std::string_view key, std::string_view detail) {
            return "Invalid config '" + std::string(path) + "' at '" + std::string(key)
                   + "': " + std::string(detail);
        }
        class ConfigReader {
        public:
            ConfigReader(const YAML::Node& root, const std::string& path)
                : m_root(root), m_path(path) {}
            template<typename T>
            bool read(std::string_view key, T& value, std::string_view expected) {
                YAML::Node node(YAML::NodeType::Undefined);
                if(!find(key, node))
                    return false;
                if(!node.IsDefined())
                    return true;
                T candidate{};
                if(!node.IsScalar() || !YAML::convert<T>::decode(node, candidate))
                    return fail(key, "expected " + std::string(expected));
                value = std::move(candidate);
                return true;
            }
            template<typename T, std::size_t Size>
            bool named(std::string_view key, T& value,
                const std::array<std::pair<const char*, T>, Size>& names) {
                YAML::Node node(YAML::NodeType::Undefined);
                if(!find(key, node))
                    return false;
                if(!node.IsDefined())
                    return true;
                if(!node.IsScalar())
                    return fail(key, "expected a string");
                const auto& name = node.Scalar();
                std::string expected;
                for(const auto& [label, candidate] : names) {
                    if(name == label) {
                        value = candidate;
                        return true;
                    }
                    if(!expected.empty())
                        expected += ", ";
                    expected += label;
                }
                return fail(key, "unknown value '" + name + "'; expected one of: " + expected);
            }
            bool samples(SampleCount& value) {
                std::uint32_t count = static_cast<std::uint32_t>(value);
                if(!read("vulkan.msaa_samples", count, "one of 1, 2, 4, 8, 16, 32, or 64"))
                    return false;
                if(count == 0 || count > 64 || (count & (count - 1)) != 0)
                    return fail(
                        "vulkan.msaa_samples", "unsupported sample count " + std::to_string(count));
                value = static_cast<SampleCount>(count);
                return true;
            }
            const std::string& error() const { return m_error; }

        private:
            bool fail(std::string_view key, std::string_view detail) {
                m_error = config_error(m_path, key, detail);
                return false;
            }
            bool find(std::string_view key, YAML::Node& output) {
                if(!m_root.IsDefined() || m_root.IsNull())
                    return true;
                YAML::Node node = m_root;
                std::stringstream stream{std::string(key)};
                std::string segment, parent;
                while(std::getline(stream, segment, '.')) {
                    if(!node.IsMap())
                        return fail(parent.empty() ? "<root>" : parent, "expected a mapping");
                    const auto child = static_cast<const YAML::Node&>(node)[segment];
                    if(!child.IsDefined())
                        return true;
                    node.reset(child);
                    if(!parent.empty())
                        parent += '.';
                    parent += segment;
                }
                output.reset(node);
                return true;
            }
            const YAML::Node& m_root;
            const std::string& m_path;
            std::string m_error;
        };
        Result<void> merge_config_file(Config& config, const std::string& path) {
            auto text = read_text_file(path);
            if(!text)
                return Result<void>::failure(text.error());
            YAML::Node root;
            try {
                root = YAML::Load(text.value());
            } catch(const YAML::Exception& error) {
                return Result<void>::failure(config_error(path, "<root>", error.what()));
            }
            if(root.IsDefined() && !root.IsNull() && !root.IsMap())
                return Result<void>::failure(config_error(path, "<root>", "expected a mapping"));
            ConfigReader reader(root, path);
            if(!reader.read("diagnostics.enable_file_logging",
                   config.diagnostics.log.enable_file_logging, "a boolean")
                || !reader.read("diagnostics.log_level", config.diagnostics.log.level, "a string")
                || !reader.read(
                    "diagnostics.enable_profiler", config.diagnostics.enable_profiler, "a boolean")
                || !reader.read("diagnostics.enable_render_diagnostics",
                    config.diagnostics.enable_render_diagnostics, "a boolean")
                || !reader.read("window.width", config.window.width, "an integer")
                || !reader.read("window.height", config.window.height, "an integer")
                || !reader.read("window.title", config.window.title, "a string")
                || !reader.read("window.fullscreen", config.window.fullscreen, "a boolean")
                || !reader.read("window.resizable", config.window.resizable, "a boolean")
                || !reader.named(
                    "vulkan.surface_format", config.vulkan.surface_format, SURFACE_FORMATS)
                || !reader.named("vulkan.color_space", config.vulkan.color_space, COLOR_SPACES)
                || !reader.named("vulkan.depth_format", config.vulkan.depth_format, DEPTH_FORMATS)
                || !reader.named("vulkan.present_mode", config.vulkan.present_mode, PRESENT_MODES)
                || !reader.read("vulkan.swapchain_image_count", config.vulkan.swapchain_image_count,
                    "a non-negative integer")
                || !reader.samples(config.vulkan.msaa_samples)
                || !reader.read(
                    "diagnostics.enable_validation", config.vulkan.enable_validation, "a boolean")
                || !reader.read("render.max_frames_in_flight", config.render.max_frames_in_flight,
                    "a non-negative integer")
                || !reader.named("render.output_mode", config.render.output_mode, OUTPUT_MODES)
                || !reader.read("render.hdr_headroom", config.render.hdr_headroom, "a number")
                || !reader.read("render.enable_vsync", config.render.enable_vsync, "a boolean")
                || !reader.read("render.max_anisotropy", config.render.max_anisotropy, "a number"))
                return Result<void>::failure(reader.error());
            return Result<void>::success();
        }
    }
    Result<Config> ConfigLoader::load(const std::string& path) const {
        return load(std::vector{path});
    }
    Result<Config> ConfigLoader::load(const std::vector<std::string>& paths) const {
        if(paths.empty())
            return Result<Config>::failure("At least one config file is required");
        Config config;
        std::string sources;
        for(const auto& path : paths) {
            if(auto result = merge_config_file(config, path); !result)
                return Result<Config>::failure(result.error());
            if(!sources.empty())
                sources += ", ";
            sources += path;
        }
        if(config.window.width <= 0)
            return Result<Config>::failure(
                config_error(sources, "window.width", "must be greater than zero"));
        if(config.window.height <= 0)
            return Result<Config>::failure(
                config_error(sources, "window.height", "must be greater than zero"));
        if(config.vulkan.swapchain_image_count == 0)
            return Result<Config>::failure(
                config_error(sources, "vulkan.swapchain_image_count", "must be greater than zero"));
        if(config.render.max_frames_in_flight == 0)
            return Result<Config>::failure(
                config_error(sources, "render.max_frames_in_flight", "must be greater than zero"));
        if(!std::isfinite(config.render.max_anisotropy) || config.render.max_anisotropy < 1.0f)
            return Result<Config>::failure(config_error(
                sources, "render.max_anisotropy", "must be a finite number of at least 1.0"));
        if(!std::isfinite(config.render.hdr_headroom) || config.render.hdr_headroom < 1.0f
            || config.render.hdr_headroom > 16.0f)
            return Result<Config>::failure(config_error(
                sources, "render.hdr_headroom", "must be a finite number between 1 and 16"));
        return Result<Config>::success(std::move(config));
    }
}
