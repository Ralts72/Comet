#include "config_loader.h"

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
        std::string get_default_config_path() {
            std::filesystem::path config_path(std::string(PROJECT_ROOT_DIR));
            config_path /= "engine/assets/config.yaml";
            return config_path.string();
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
    }

    Config ConfigLoader::load(const std::string& config_path) const {
        const std::string resolved_path = config_path.empty() ? get_default_config_path() : config_path;
        if(!std::filesystem::exists(resolved_path)) {
            throw std::runtime_error("Config file not found: " + resolved_path);
        }

        YAML::Node root;
        try {
            root = YAML::LoadFile(resolved_path);
        } catch(const YAML::Exception& error) {
            throw std::runtime_error(
                "Failed to load config '" + resolved_path + "': " + std::string(error.what()));
        }

        if(root.IsDefined() && !root.IsNull() && !root.IsMap()) {
            throw config_error(resolved_path, "<root>", "expected a mapping");
        }

        Config config;
        config.log.enable_file_logging = read_value<bool>(
            root, "debug.enable_file_logging", config.log.enable_file_logging, "a boolean", resolved_path);
        config.log.level = read_value<std::string>(
            root, "debug.log_level", config.log.level, "a string", resolved_path);

        config.window.width = read_value<int>(
            root, "window.width", config.window.width, "an integer", resolved_path);
        config.window.height = read_value<int>(
            root, "window.height", config.window.height, "an integer", resolved_path);
        config.window.title = read_value<std::string>(
            root, "window.title", config.window.title, "a string", resolved_path);
        config.window.fullscreen = read_value<bool>(
            root, "window.fullscreen", config.window.fullscreen, "a boolean", resolved_path);
        config.window.resizable = read_value<bool>(
            root, "window.resizable", config.window.resizable, "a boolean", resolved_path);

        config.vulkan.surface_format = read_value<int>(
            root, "vulkan.surface_format", config.vulkan.surface_format, "an integer", resolved_path);
        config.vulkan.color_space = read_value<int>(
            root, "vulkan.color_space", config.vulkan.color_space, "an integer", resolved_path);
        config.vulkan.depth_format = read_value<int>(
            root, "vulkan.depth_format", config.vulkan.depth_format, "an integer", resolved_path);
        config.vulkan.present_mode = read_value<int>(
            root, "vulkan.present_mode", config.vulkan.present_mode, "an integer", resolved_path);
        config.vulkan.swapchain_image_count = read_value<std::uint32_t>(
            root,
            "vulkan.swapchain_image_count",
            config.vulkan.swapchain_image_count,
            "a non-negative integer",
            resolved_path);
        config.vulkan.msaa_samples = read_value<int>(
            root, "vulkan.msaa_samples", config.vulkan.msaa_samples, "an integer", resolved_path);
        config.vulkan.enable_validation = read_value<bool>(
            root, "debug.enable_validation", config.vulkan.enable_validation, "a boolean", resolved_path);

        config.render.max_frames_in_flight = read_value<std::uint32_t>(
            root,
            "render.max_frames_in_flight",
            config.render.max_frames_in_flight,
            "a non-negative integer",
            resolved_path);
        config.render.clear_color = read_clear_color(root, config.render, resolved_path);
        config.render.enable_vsync = read_value<bool>(
            root, "render.enable_vsync", config.render.enable_vsync, "a boolean", resolved_path);
        config.render.max_anisotropy = read_value<float>(
            root, "render.max_anisotropy", config.render.max_anisotropy, "a number", resolved_path);

        validate_config(config, resolved_path);
        return config;
    }
}
