#pragma once
#include "common/export.h"
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace Comet {
    class COMET_API Config {
    public:
        struct Log {
            bool enable_file_logging = true;
            std::string level = "trace";
        };

        struct Window {
            int width = 1280;
            int height = 720;
            std::string title = "Comet Engine";
            bool fullscreen = false;
            bool resizable = true;
        };

        struct Vulkan {
            int surface_format = 50;
            int color_space = 0;
            int depth_format = 126;
            int present_mode = 0;
            std::uint32_t swapchain_image_count = 3;
            int msaa_samples = 4;
            bool enable_validation = true;
        };

        struct Render {
            std::uint32_t max_frames_in_flight = 2;
            std::array<float, 4> clear_color = {0.2f, 0.4f, 0.1f, 1.0f};
            bool enable_vsync = false;
        };

        struct Runtime {
            Log log;
            Window window;
            Vulkan vulkan;
            Render render;
        };

        Config() = default;

        ~Config() = default;

        Config(const Config&) = delete;

        Config& operator=(const Config&) = delete;

        Runtime load_runtime_config(const std::string& config_path = "");

    private:
        void load(const std::string& config_path);

        Runtime get_runtime_config() const;

        template<typename T>
        T get(const std::string& key) const;

        template<typename T>
        T get(const std::string& key, const T& default_value) const;

        static std::string get_default_config_path();

        void load_from_file(const std::string& config_path);

        YAML::Node get_node_internal(const std::string& key) const;

        YAML::Node m_root;
        std::string m_config_path;
        mutable std::mutex m_mutex;
    };

    template<typename T>
    T Config::get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_mutex);

        const YAML::Node node = get_node_internal(key);
        if(!node) {
            throw std::runtime_error("Config key not found: " + key);
        }

        try {
            return node.as<T>();
        } catch(const YAML::Exception& e) {
            throw std::runtime_error("Config type conversion failed for key '" + key + "': " + e.what());
        }
    }

    template<typename T>
    T Config::get(const std::string& key, const T& default_value) const {
        try {
            return get<T>(key);
        } catch(const std::runtime_error&) {
            return default_value;
        }
    }
}
