#include "common/file_io.h"

#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>

namespace Comet::Tests {
    class FileIoTest: public testing::Test {
    protected:
        TemporaryDirectory directory;
        const std::filesystem::path root = directory.path();
    };

    TEST_F(FileIoTest, AtomicallyCreatesAndReplacesTextFiles) {
        const auto path = root / "nested/asset.mat";
        for(const std::string contents : {"first\n", "second value\n"}) {
            const auto saved = write_text_file_atomic(path, contents);
            ASSERT_TRUE(saved) << saved.error();
            const auto stored = read_text_file(path);
            ASSERT_TRUE(stored) << stored.error();
            EXPECT_EQ(stored.value(), contents);
        }
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(path.parent_path()),
                      std::filesystem::directory_iterator{}),
            1);
    }

    TEST_F(FileIoTest, AtomicallyWritesBinaryFiles) {
        const auto path = root / "nested/mesh.bin";
        constexpr std::array contents{std::byte{0x00}, std::byte{0x7F}, std::byte{0xFF}};
        const auto saved = write_binary_file_atomic(path, contents);
        ASSERT_TRUE(saved) << saved.error();
        const auto stored = read_text_file(path);
        ASSERT_TRUE(stored) << stored.error();
        ASSERT_EQ(stored.value().size(), contents.size());
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[0]), 0x00);
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[1]), 0x7F);
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[2]), 0xFF);
    }

    TEST_F(FileIoTest, ReadsEmptyAndMultiBlockFilesWithoutTreatingEofAsFailure) {
        const auto path = root / "contents.txt";
        for(const size_t size : {0u, 8192u, 8193u, 20000u}) {
            SCOPED_TRACE(size);
            std::string contents(size, 'x');
            if(size)
                contents.back() = '\0';
            ASSERT_TRUE(write_text_file_atomic(path, contents));
            const auto stored = read_text_file(path);
            ASSERT_TRUE(stored) << stored.error();
            EXPECT_EQ(stored.value(), contents);
        }
    }

    TEST_F(FileIoTest, RejectsMissingFilesAndDirectoryReads) {
        const auto missing = read_text_file(root / "missing");
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find((root / "missing").string()), std::string::npos);
        const auto folder = read_text_file(root);
        ASSERT_FALSE(folder);
        EXPECT_NE(folder.error().find(root.string()), std::string::npos);
        EXPECT_FALSE(read_text_file({}));
    }

    TEST_F(FileIoTest, RejectsInvalidWritePathsWithoutChangingExistingFiles) {
        const auto path = root / "existing";
        ASSERT_TRUE(write_text_file_atomic(path, "keep"));
        EXPECT_FALSE(write_text_file_atomic({}, "replacement"));
        const auto saved = write_text_file_atomic(path / "child", "replacement");
        ASSERT_FALSE(saved);
        EXPECT_NE(saved.error().find(path.string()), std::string::npos);
        const auto stored = read_text_file(path);
        ASSERT_TRUE(stored);
        EXPECT_EQ(stored.value(), "keep");
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root),
                      std::filesystem::directory_iterator{}),
            1);
    }

    TEST_F(FileIoTest, FailedReplacementRemovesTemporaryFileAndPreservesDestination) {
        const auto destination = root / "occupied";
        ASSERT_TRUE(write_text_file_atomic(destination / "keep", "original"));
        // The temporary write succeeds, but a file cannot replace a non-empty directory.
        const auto saved = write_text_file_atomic(destination, "replacement");
        ASSERT_FALSE(saved);
        EXPECT_NE(saved.error().find("atomically replace"), std::string::npos);
        EXPECT_NE(saved.error().find(destination.string()), std::string::npos);
        const auto stored = read_text_file(destination / "keep");
        ASSERT_TRUE(stored);
        EXPECT_EQ(stored.value(), "original");
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root),
                      std::filesystem::directory_iterator{}),
            1);
    }
}
