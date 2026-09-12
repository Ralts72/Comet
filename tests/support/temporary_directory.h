#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace Comet::Tests {
    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            const auto parent =
                std::filesystem::canonical(std::filesystem::temp_directory_path());
            for(int attempt = 0; attempt < 32; ++attempt) {
                auto candidate =
                    parent / ("comet_test_" + std::to_string(std::random_device{}()));
                if(std::filesystem::create_directory(candidate)) {
                    m_path = std::move(candidate);
                    return;
                }
            }
            throw std::runtime_error("Cannot create unique test directory");
        }
        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    private:
        std::filesystem::path m_path;
    };
}
