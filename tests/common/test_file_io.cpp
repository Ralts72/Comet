#include "common/file_io.h"

#include "asset/handle.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace Comet::Tests {
    namespace {
        std::string read_file(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            return contents.str();
        }
    }

    TEST(FileIoTest, AtomicallyCreatesAndReplacesTextFiles) {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_file_io_test_"
               + std::to_string(AssetHandle::generate().value()));
        const std::filesystem::path path = root / "nested" / "asset.yaml";

        write_text_file_atomic(path, "first\n");
        EXPECT_EQ(read_file(path), "first\n");

        write_text_file_atomic(path, "second value\n");
        EXPECT_EQ(read_file(path), "second value\n");

        std::size_t file_count = 0;
        for(const auto& entry:
            std::filesystem::directory_iterator(path.parent_path())) {
            if(entry.is_regular_file()) {
                ++file_count;
            }
        }
        EXPECT_EQ(file_count, 1u);

        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
}
