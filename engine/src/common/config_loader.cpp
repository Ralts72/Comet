#include "config_loader.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace Comet {
    namespace {
        constexpr std::array SURFACE_FORMATS = {
            std::pair{"bgra8_srgb", Format::B8G8R8A8_SRGB},
            std::pair{"bgra8_unorm", Format::B8G8R8A8_UNORM},
            std::pair{"rgba8_srgb", Format::R8G8B8A8_SRGB},
            std::pair{"rgba8_unorm", Format::R8G8B8A8_UNORM}
        };

        constexpr std::array COLOR_SPACES = {
            std::pair{"srgb_nonlinear", ImageColorSpace::SrgbNonlinearKHR}
        };

        constexpr std::array DEPTH_FORMATS = {
            std::pair{"d32_float", Format::D32_SFLOAT},
            std::pair{"d24_unorm_s8_uint", Format::D24_UNORM_S8_UINT},
            std::pair{"d32_float_s8_uint", Format::D32_SFLOAT_S8_UINT}
        };

        constexpr std::array PRESENT_MODES = {
            std::pair{"immediate", PresentMode::Immediate},
            std::pair{"mailbox", PresentMode::Mailbox},
            std::pair{"fifo", PresentMode::Fifo},
            std::pair{"fifo_relaxed", PresentMode::FifoRelaxed}
        };

        std::vector<std::string> get_default_config_paths() {
            std::filesystem::path config_directory(std::string(PROJECT_ROOT_DIR));
            config_directory /= "engine/assets/config";
            return {
                (config_directory / "common.yaml").string(),
                (config_directory / "profiles" / (std::string(COMET_CONFIG_PROFILE) + ".yaml")).string()
            };
        }

        std::runtime_error config_error(const std::string& config_path,
                                        const std::string_view key,
                                        const std::string& detail) {
            return std::runtime_error(
                "Invalid config '" + config_path + "' at '" + std::string(key) + "': " + detail);
        }

        std::optional<YAML::Node> find_node(const YAML::Node& root,
                                            const std::string_view key,
                                            const std::string& config_path) {
            if(!root.IsDefined() || root.IsNull()) {
                return std::nullopt;
            }

            YAML::Node node = root;
            std::stringstream key_stream{std::string(key)};
            std::string segment;
            std::string parent_path;

            while(std::getline(key_stream, segment, '.')) {
                if(!node.IsMap()) {
                    const std::string location = parent_path.empty() ? "<root>" : parent_path;
                    throw config_error(config_path, location, "expected a mapping");
                }

                const YAML::Node child = static_cast<const YAML::Node&>(node)[segment];
                if(!child.IsDefined()) {
                    return std::nullopt;
                }

                node.reset(child);
                if(!parent_path.empty()) {
                    parent_path += '.';
                }
                parent_path += segment;
            }

            return node;
        }

        template<typename T>
        T read_value(const YAML::Node& root,
                     const std::string_view key,
                     T default_value,
                     const std::string_view expected_type,
                     const std::string& config_path) {
            const auto node = find_node(root, key, config_path);
            if(!node.has_value()) {
                return default_value;
            }

            try {
                return node->as<T>();
            } catch(const YAML::Exception& error) {
                throw config_error(
                    config_path,
                    key,
                    "expected " + std::string(expected_type) + ", got " + YAML::Dump(*node)
                    + " (" + error.what() + ")");
            }
        }

        template<typename T, std::size_t Size>
        T read_named_value(const YAML::Node& root,
                           const std::string_view key,
                           const T default_value,
                           const std::array<std::pair<const char*, T>, Size>& values,
                           const std::string& config_path) {
            if(!find_node(root, key, config_path).has_value()) {
                return default_value;
            }

            const auto name = read_value<std::string>(
                root, key, {}, "a string", config_path);
            for(const auto& [candidate, value]: values) {
                if(name == candidate) {
                    return value;
                }
            }

            std::string expected;
            for(const auto& [candidate, value]: values) {
                static_cast<void>(value);
                if(!expected.empty()) {
                    expected += ", ";
                }
                expected += candidate;
            }
            throw config_error(
                config_path,
                key,
                "unknown value '" + name + "'; expected one of: " + expected);
        }

        SampleCount read_sample_count(const YAML::Node& root,
                                      const std::string_view key,
                                      const SampleCount default_value,
                                      const std::string& config_path) {
            if(!find_node(root, key, config_path).has_value()) {
                return default_value;
            }

            switch(const auto value = read_value<std::uint32_t>(
                       root, key, 0, "one of 1, 2, 4, 8, 16, 32, or 64", config_path)) {
                case 1: return SampleCount::Count1;
                case 2: return SampleCount::Count2;
                case 4: return SampleCount::Count4;
                case 8: return SampleCount::Count8;
                case 16: return SampleCount::Count16;
                case 32: return SampleCount::Count32;
                case 64: return SampleCount::Count64;
                default:
                    throw config_error(
                        config_path,
                        key,
                        "unsupported sample count " + std::to_string(value)
                        + "; expected one of: 1, 2, 4, 8, 16, 32, 64");
            }
        }

        std::array<float, 4> read_clear_color(const YAML::Node& root,
                                              const Config::Render& defaults,
                                              const std::string& config_path) {
            if(!find_node(root, "render.clear_color", config_path).has_value()) {
                return defaults.clear_color;
            }

            const auto values = read_value<std::vector<float>>(
                root, "render.clear_color", {}, "an array of four numbers", config_path);
            if(values.size() != 4) {
                throw config_error(
                    config_path,
                    "render.clear_color",
                    "expected an array of four numbers, got " + std::to_string(values.size()) + " values");
            }

            return {values[0], values[1], values[2], values[3]};
        }

        void validate_config(const Config& config, const std::string& config_path) {
            if(config.window.width <= 0) {
                throw config_error(config_path, "window.width", "must be greater than zero");
            }
            if(config.window.height <= 0) {
                throw config_error(config_path, "window.height", "must be greater than zero");
            }
            if(config.vulkan.swapchain_image_count == 0) {
                throw config_error(config_path, "vulkan.swapchain_image_count", "must be greater than zero");
            }
            if(config.render.max_frames_in_flight == 0) {
                throw config_error(config_path, "render.max_frames_in_flight", "must be greater than zero");
            }
            if(!std::isfinite(config.render.max_anisotropy)
                || config.render.max_anisotropy < 1.0f) {
                throw config_error(
                    config_path, "render.max_anisotropy", "must be a finite number of at least 1.0");
            }
        }

        void merge_config_file(Config& config, const std::string& config_path) {
            if(!std::filesystem::exists(config_path)) {
                throw std::runtime_error("Config file not found: " + config_path);
            }

            YAML::Node root;
            try {
                root = YAML::LoadFile(config_path);
            } catch(const YAML::Exception& error) {
                throw std::runtime_error(
                    "Failed to load config '" + config_path + "': " + std::string(error.what()));
            }

            if(root.IsDefined() && !root.IsNull() && !root.IsMap()) {
                throw config_error(config_path, "<root>", "expected a mapping");
            }

            config.diagnostics.log.enable_file_logging = read_value<bool>(
                root,
                "diagnostics.enable_file_logging",
                config.diagnostics.log.enable_file_logging,
                "a boolean",
                config_path);
            config.diagnostics.log.level = read_value<std::string>(
                root,
                "diagnostics.log_level",
                config.diagnostics.log.level,
                "a string",
                config_path);
            config.diagnostics.enable_profiler = read_value<bool>(
                root,
                "diagnostics.enable_profiler",
                config.diagnostics.enable_profiler,
                "a boolean",
                config_path);

            config.window.width = read_value<int>(
                root, "window.width", config.window.width, "an integer", config_path);
            config.window.height = read_value<int>(
                root, "window.height", config.window.height, "an integer", config_path);
            config.window.title = read_value<std::string>(
                root, "window.title", config.window.title, "a string", config_path);
            config.window.fullscreen = read_value<bool>(
                root, "window.fullscreen", config.window.fullscreen, "a boolean", config_path);
            config.window.resizable = read_value<bool>(
                root, "window.resizable", config.window.resizable, "a boolean", config_path);

            config.vulkan.surface_format = read_named_value(
                root, "vulkan.surface_format", config.vulkan.surface_format, SURFACE_FORMATS, config_path);
            config.vulkan.color_space = read_named_value(
                root, "vulkan.color_space", config.vulkan.color_space, COLOR_SPACES, config_path);
            config.vulkan.depth_format = read_named_value(
                root, "vulkan.depth_format", config.vulkan.depth_format, DEPTH_FORMATS, config_path);
            config.vulkan.present_mode = read_named_value(
                root, "vulkan.present_mode", config.vulkan.present_mode, PRESENT_MODES, config_path);
            config.vulkan.swapchain_image_count = read_value<std::uint32_t>(
                root,
                "vulkan.swapchain_image_count",
                config.vulkan.swapchain_image_count,
                "a non-negative integer",
                config_path);
            config.vulkan.msaa_samples = read_sample_count(
                root, "vulkan.msaa_samples", config.vulkan.msaa_samples, config_path);
            config.vulkan.enable_validation = read_value<bool>(
                root,
                "diagnostics.enable_validation",
                config.vulkan.enable_validation,
                "a boolean",
                config_path);

            config.render.max_frames_in_flight = read_value<std::uint32_t>(
                root,
                "render.max_frames_in_flight",
                config.render.max_frames_in_flight,
                "a non-negative integer",
                config_path);
            config.render.clear_color = read_clear_color(root, config.render, config_path);
            config.render.enable_vsync = read_value<bool>(
                root, "render.enable_vsync", config.render.enable_vsync, "a boolean", config_path);
            config.render.max_anisotropy = read_value<float>(
                root, "render.max_anisotropy", config.render.max_anisotropy, "a number", config_path);
        }
    }

    Config ConfigLoader::load() const {
        return load(get_default_config_paths());
    }

    Config ConfigLoader::load(const std::string& config_path) const {
        return load(std::vector{config_path});
    }

    Config ConfigLoader::load(const std::vector<std::string>& config_paths) const {
        if(config_paths.empty()) {
            throw std::runtime_error("At least one config file is required");
        }

        Config config;
        std::string source_description;
        for(const auto& config_path: config_paths) {
            merge_config_file(config, config_path);
            if(!source_description.empty()) {
                source_description += ", ";
            }
            source_description += config_path;
        }

        validate_config(config, source_description);
        return config;
    }
}
