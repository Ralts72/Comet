#include "pch.h"
#include "config.h"
#include <iostream>

namespace Comet {
    Config::Config() {
        std::filesystem::path config_path(std::string(PROJECT_ROOT_DIR));
        config_path /= "engine/assets/config.yaml";
        load(config_path.string());
    }

    Config& Config::instance() {
        static Config s_instance;
        return s_instance;
    }

    void Config::reload(const std::string& config_path) {
        auto& cfg = instance();
        std::lock_guard<std::mutex> lock(cfg.m_mutex);

        if (config_path.empty()) {
            std::filesystem::path default_path(std::string(PROJECT_ROOT_DIR));
            default_path /= "engine/assets/config.yaml";
            cfg.load(default_path.string());
        } else {
            cfg.load(config_path);
        }
    }

    bool Config::has(const std::string& key) {
        auto& cfg = instance();
        std::lock_guard<std::mutex> lock(cfg.m_mutex);

        try {
            YAML::Node node = cfg.get_node_internal(key);
            return node.IsDefined() && !node.IsNull();
        } catch (...) {
            return false;
        }
    }

    YAML::Node Config::get_node(const std::string& key) {
        auto& cfg = instance();
        std::lock_guard<std::mutex> lock(cfg.m_mutex);
        return cfg.get_node_internal(key);
    }

    void Config::load(const std::string& config_path) {
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