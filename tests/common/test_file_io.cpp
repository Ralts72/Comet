#include "common/file_io.h"

#include "asset/handle.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Comet::Tests {
    TEST(FileIoTest, AtomicallyCreatesAndReplacesTextFiles) {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_file_io_test_" + std::to_string(AssetHandle::generate().value()));
        const std::filesystem::path path = root / "nested" / "asset.yaml";

        write_text_file_atomic(path, "first\n");
        EXPECT_EQ(read_text_file(path), "first\n");

        write_text_file_atomic(path, "second value\n");
        EXPECT_EQ(read_text_file(path), "second value\n");

        std::size_t file_count = 0;
        for(const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
            if(entry.is_regular_file()) {
                ++file_count;
            }
        }
        EXPECT_EQ(file_count, 1u);

        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    TEST(FileIoTest, AtomicallyWritesBinaryFiles) {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_binary_file_io_test_"
                + std::to_string(AssetHandle::generate().value()));
        const std::filesystem::path path = root / "nested" / "mesh.bin";
        constexpr std::array contents{std::byte{0x00}, std::byte{0x7F}, std::byte{0xFF}};

        write_binary_file_atomic(path, contents);

        const std::string stored = read_text_file(path);
        ASSERT_EQ(stored.size(), contents.size());
        EXPECT_EQ(static_cast<unsigned char>(stored[0]), 0x00);
        EXPECT_EQ(static_cast<unsigned char>(stored[1]), 0x7F);
        EXPECT_EQ(static_cast<unsigned char>(stored[2]), 0xFF);

        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    TEST(FileIoTest, RejectsMissingTextFile) {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path()
            / ("comet_missing_file_io_test_"
                + std::to_string(AssetHandle::generate().value()));

        EXPECT_THROW(static_cast<void>(read_text_file(path)), std::runtime_error);
    }
}
