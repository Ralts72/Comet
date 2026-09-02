#include "common/file_io.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Comet {
    namespace {
        std::filesystem::path temporary_path_for(
            const std::filesystem::path& path) {
            static std::atomic<std::uint64_t> sequence = 0;
            const std::string temporary_name = ".comet-tmp-"
                + path.filename().string() + "."
                + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count())
                + "." + std::to_string(sequence.fetch_add(1));
            return path.parent_path() / temporary_name;
        }

        void replace_file(
            const std::filesystem::path& source,
            const std::filesystem::path& destination) {
#ifdef _WIN32
            if(!MoveFileExW(
                   source.c_str(),
                   destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                throw std::runtime_error(
                    "Failed to atomically replace file '"
                    + destination.string() + "': Windows error "
                    + std::to_string(GetLastError()));
            }
#else
            std::error_code error;
            std::filesystem::rename(source, destination, error);
            if(error) {
                throw std::runtime_error(
                    "Failed to atomically replace file '"
                    + destination.string() + "': " + error.message());
            }
#endif
        }

        void write_file_atomic(
            const std::filesystem::path& path,
            const char* contents,
            const std::size_t size) {
            if(path.empty()) {
                throw std::runtime_error("Cannot write an empty file path");
            }

            const std::filesystem::path parent = path.parent_path();
            if(!parent.empty()) {
                std::error_code error;
                std::filesystem::create_directories(parent, error);
                if(error) {
                    throw std::runtime_error(
                        "Failed to create directory '" + parent.string()
                        + "': " + error.message());
                }
            }

            const std::filesystem::path temporary = temporary_path_for(path);
            try {
                std::ofstream output(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if(!output) {
                    throw std::runtime_error(
                        "Failed to open temporary file for writing: "
                        + temporary.string());
                }
                if(size != 0) {
                    output.write(
                        contents,
                        static_cast<std::streamsize>(size));
                }
                output.flush();
                if(!output) {
                    throw std::runtime_error(
                        "Failed to write temporary file: "
                        + temporary.string());
                }
                output.close();
                if(!output) {
                    throw std::runtime_error(
                        "Failed to close temporary file: "
                        + temporary.string());
                }

                replace_file(temporary, path);
            } catch(...) {
                std::error_code cleanup_error;
                std::filesystem::remove(temporary, cleanup_error);
                throw;
            }
        }
    }

    std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error(
                "Failed to open file '" + path.string() + "'");
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        if(input.bad()) {
            throw std::runtime_error(
                "Failed to read file '" + path.string() + "'");
        }
        return contents.str();
    }

    void write_binary_file_atomic(
        const std::filesystem::path& path,
        const std::span<const std::byte> contents) {
        write_file_atomic(
            path,
            reinterpret_cast<const char*>(contents.data()),
            contents.size());
    }

    void write_text_file_atomic(
        const std::filesystem::path& path,
        const std::string_view contents) {
        write_file_atomic(path, contents.data(), contents.size());
    }
}
