#include "pch.h"
#include "config.h"

namespace Comet {
    namespace {
        std::array<float, 4> parse_clear_color(const std::vector<float>& values) {
            return {
                !values.empty() ? values[0] : 0.2f,
                values.size() > 1 ? values[1] : 0.4f,
                values.size() > 2 ? values[2] : 0.1f,
                values.size() > 3 ? values[3] : 1.0f
            };
        }
    }

    std::string Config::get_default_config_path() {
        std::filesystem::path config_path(std::string(PROJECT_ROOT_DIR));
        config_path /= "engine/assets/config.yaml";
        return config_path.string();
    }

    void Config::load(const std::string& config_path) {
        std::lock_guard<std::mutex> lock(m_mutex);

        load_from_file(config_path.empty() ? get_default_config_path() : config_path);
    }

    Config::Runtime Config::load_runtime_config(const std::string& config_path) {
        load(config_path);
        return get_runtime_config();
    }

    Config::Runtime Config::get_runtime_config() const {
        Runtime config;

        config.log.enable_file_logging = get<bool>("debug.enable_file_logging", config.log.enable_file_logging);
        config.log.level = get<std::string>("debug.log_level", config.log.level);

        config.window.width = get<int>("window.width", config.window.width);
        config.window.height = get<int>("window.height", config.window.height);
        config.window.title = get<std::string>("window.title", config.window.title);
        config.window.fullscreen = get<bool>("window.fullscreen", config.window.fullscreen);
        config.window.resizable = get<bool>("window.resizable", config.window.resizable);

        config.vulkan.surface_format = get<int>("vulkan.surface_format", config.vulkan.surface_format);
        config.vulkan.color_space = get<int>("vulkan.color_space", config.vulkan.color_space);
        config.vulkan.depth_format = get<int>("vulkan.depth_format", config.vulkan.depth_format);
        config.vulkan.present_mode = get<int>("vulkan.present_mode", config.vulkan.present_mode);
        config.vulkan.swapchain_image_count = get<std::uint32_t>(
            "vulkan.swapchain_image_count", config.vulkan.swapchain_image_count);
        config.vulkan.msaa_samples = get<int>("vulkan.msaa_samples", config.vulkan.msaa_samples);
        config.vulkan.enable_validation = get<bool>("debug.enable_validation", config.vulkan.enable_validation);

        config.render.max_frames_in_flight = get<std::uint32_t>(
            "render.max_frames_in_flight", config.render.max_frames_in_flight);
        config.render.clear_color = parse_clear_color(
            get<std::vector<float>>("render.clear_color", {0.2f, 0.4f, 0.1f, 1.0f}));
        config.render.enable_vsync = get<bool>("render.enable_vsync", config.render.enable_vsync);

        return config;
    }

    void Config::load_from_file(const std::string& config_path) {
        try {
            if (!std::filesystem::exists(config_path)) {
                std::cerr << "[Config] Error: Config file not found: " << config_path << std::endl;
                throw std::runtime_error("Config file not found: " + config_path);
            }

            m_root = YAML::LoadFile(config_path);
            m_config_path = config_path;

#ifdef BUILD_TYPE_DEBUG
            std::cout << "[Config] Loaded successfully from: " << config_path << std::endl;
#endif
        } catch (const YAML::Exception& e) {
            std::cerr << "[Config] Error: Failed to load config file '" << config_path << "': " << e.what() << std::endl;
            throw std::runtime_error("Failed to load config: " + std::string(e.what()));
        }
    }

    YAML::Node Config::get_node_internal(const std::string& key) const {
        std::vector<std::string> keys;
        std::stringstream ss(key);
        std::string token;

        while (std::getline(ss, token, '.')) {
            if (!token.empty()) {
                keys.push_back(token);
            }
        }

        if (keys.empty()) {
            return {};
        }

        // 逐层访问 YAML 节点
        YAML::Node node = YAML::Clone(m_root);
        for (const auto& k : keys) {
            if (!node.IsDefined() || node.IsNull() || !node.IsMap()) {
                return {};
            }
            node = node[k];
        }

        return node;
    }
}
