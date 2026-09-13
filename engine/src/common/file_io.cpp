#include "common/file_io.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Comet {
    namespace {
        std::filesystem::path temporary_path_for(const std::filesystem::path& path) {
            static std::atomic<std::uint64_t> sequence = 0;
            const std::string temporary_name =
                ".comet-tmp-" + path.filename().string() + "."
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "."
                + std::to_string(sequence.fetch_add(1));
            return path.parent_path() / temporary_name;
        }

        struct TemporaryFile {
            std::filesystem::path path;
            bool published = false;

            explicit TemporaryFile(std::filesystem::path file_path) : path(std::move(file_path)) {}
            TemporaryFile(const TemporaryFile&) = delete;
            TemporaryFile& operator=(const TemporaryFile&) = delete;

            ~TemporaryFile() {
                if(!published) {
                    std::error_code error;
                    std::filesystem::remove(path, error);
                }
            }
        };

        Result<void> replace_file(
            const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
            if(!MoveFileExW(source.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const auto error = GetLastError();
                return Result<void>::failure("Failed to atomically replace file '"
                                             + destination.string() + "': Windows error "
                                             + std::to_string(error));
            }
#else
            std::error_code error;
            std::filesystem::rename(source, destination, error);
            if(error) {
                return Result<void>::failure("Failed to atomically replace file '"
                                             + destination.string() + "': " + error.message());
            }
#endif
            return Result<void>::success();
        }

        Result<void> write_file_atomic(
            const std::filesystem::path& path, const char* contents, const std::size_t size) {
            if(path.empty()) {
                return Result<void>::failure("Cannot write an empty file path");
            }
            if(size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
                return Result<void>::failure("File contents are too large: " + path.string());

            const std::filesystem::path parent = path.parent_path();
            if(!parent.empty()) {
                std::error_code error;
                std::filesystem::create_directories(parent, error);
                if(error) {
                    return Result<void>::failure(
                        "Failed to create directory '" + parent.string() + "': " + error.message());
                }
            }

            TemporaryFile temporary{temporary_path_for(path)};
            // Close the stream before the temporary file is removed, including on failure.
            std::ofstream output(temporary.path, std::ios::binary | std::ios::trunc);
            if(!output) {
                return Result<void>::failure(
                    "Failed to open temporary file for writing: " + temporary.path.string());
            }
            if(size != 0)
                output.write(contents, static_cast<std::streamsize>(size));
            output.flush();
            if(!output) {
                return Result<void>::failure(
                    "Failed to write temporary file: " + temporary.path.string());
            }
            output.close();
            if(!output) {
                return Result<void>::failure(
                    "Failed to close temporary file: " + temporary.path.string());
            }

            auto result = replace_file(temporary.path, path);
            temporary.published = static_cast<bool>(result);
            return result;
        }
    }

    Result<std::string> read_text_file(const std::filesystem::path& path) {
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if(error)
            return Result<std::string>::failure(
                "Failed to inspect file '" + path.string() + "': " + error.message());
        if(!std::filesystem::is_regular_file(status))
            return Result<std::string>::failure("Not a regular file: " + path.string());

        std::ifstream input(path, std::ios::binary);
        if(!input) {
            return Result<std::string>::failure("Failed to open file '" + path.string() + "'");
        }

        std::string contents;
        std::array<char, 8192> buffer;
        do {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            contents.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
        } while(input);
        if(input.bad() || !input.eof()) {
            return Result<std::string>::failure("Failed to read file '" + path.string() + "'");
        }
        return Result<std::string>::success(std::move(contents));
    }

    Result<void> write_binary_file_atomic(
        const std::filesystem::path& path, const std::span<const std::byte> contents) {
        return write_file_atomic(
            path, reinterpret_cast<const char*>(contents.data()), contents.size());
    }

    Result<void> write_text_file_atomic(
        const std::filesystem::path& path, const std::string_view contents) {
        return write_file_atomic(path, contents.data(), contents.size());
    }
}
