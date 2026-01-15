#pragma once
#include "common/export.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <mutex>

namespace Comet {
    class COMET_API Config {
    public:
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        static Config& instance();

        static void reload(const std::string& config_path = "");

        template<typename T>
        static T get(const std::string& key);

        template<typename T>
        static T get(const std::string& key, const T& default_value);

        static bool has(const std::string& key);

        static YAML::Node get_node(const std::string& key);

    private:
        Config();
        ~Config() = default;

        void load(const std::string& config_path);
        YAML::Node get_node_internal(const std::string& key) const;

        YAML::Node m_root;
        std::string m_config_path;
        mutable std::mutex m_mutex;
    };

    template<typename T>
    T Config::get(const std::string& key) {
        const auto& cfg = instance();
        std::lock_guard<std::mutex> lock(cfg.m_mutex);

        const YAML::Node node = cfg.get_node_internal(key);
        if (!node) {
            throw std::runtime_error("Config key not found: " + key);
        }

        try {
            return node.as<T>();
        } catch (const YAML::Exception& e) {
            throw std::runtime_error("Config type conversion failed for key '" + key + "': " + e.what());
        }
    }

    template<typename T>
    T Config::get(const std::string& key, const T& default_value) {
        try {
            return get<T>(key);
        } catch (const std::runtime_error&) {
            return default_value;
        }
    }
}