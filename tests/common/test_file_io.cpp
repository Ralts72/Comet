#include "common/binary.h"
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
        const std::array<std::span<const std::byte>, 3> chunks{
            std::span(contents).first(1), {}, std::span(contents).subspan(1)};
        const auto saved = write_binary_file_atomic(path, chunks);
        ASSERT_TRUE(saved) << saved.error();
        const auto stored = read_text_file(path);
        ASSERT_TRUE(stored) << stored.error();
        ASSERT_EQ(stored.value().size(), contents.size());
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[0]), 0x00);
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[1]), 0x7F);
        EXPECT_EQ(static_cast<unsigned char>(stored.value()[2]), 0xFF);
        const auto binary = read_binary_file(path, contents.size());
        ASSERT_TRUE(binary) << binary.error();
        EXPECT_EQ(binary.value(), (std::vector<std::byte>(contents.begin(), contents.end())));
        EXPECT_FALSE(read_binary_file(path, contents.size() - 1));
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
            const auto binary = read_binary_file(path, size);
            ASSERT_TRUE(binary) << binary.error();
            EXPECT_EQ(binary.value().size(), size);
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
        EXPECT_FALSE(read_binary_file(root / "missing", 1024));
        EXPECT_FALSE(read_binary_file(root, 1024));
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
        // 临时文件写入成功，但最后不能用文件替换非空目录。
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

    TEST(BinaryCodecTest, PreservesLittleEndianIntegerFloatAndStringEncoding) {
        constexpr std::array<std::uint8_t, 33> encoding{0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab,
            0x89, 0x67, 0x45, 0x23, 0x01, 0x00, 0x00, 0xc0, 0xbf, 0x02, 0x00, 0x00, 0x00, 0x00,
            0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
        const auto bytes = std::as_bytes(std::span(encoding));
        Binary::Reader reader(bytes);
        std::uint32_t small = 0;
        std::uint64_t large = 0;
        float number = 0;
        std::string text;
        ASSERT_TRUE(reader.read_u32(small));
        ASSERT_TRUE(reader.read_u64(large));
        ASSERT_TRUE(reader.read_float(number));
        EXPECT_EQ(small, 0x12345678u);
        EXPECT_EQ(large, 0x0123456789abcdefull);
        EXPECT_FLOAT_EQ(number, -1.5f);
        ASSERT_TRUE(reader.read_string(text, 2));
        EXPECT_EQ(text, std::string("\0\xff", 2));
        ASSERT_TRUE(reader.read_string<std::uint64_t>(text, 3));
        EXPECT_EQ(text, "abc");
        EXPECT_EQ(reader.remaining(), 0);
        EXPECT_FALSE(reader.read_u32(small));
        Binary::Writer writer;
        writer.write_u32(0x12345678u);
        writer.write_u64(0x0123456789abcdefull);
        writer.write_float(-1.5f);
        ASSERT_TRUE(writer.write_string(std::string_view("\0\xff", 2), 2));
        ASSERT_TRUE(writer.write_string<std::uint64_t>("abc", 3));
        EXPECT_EQ(writer.data(), (std::vector<std::byte>(bytes.begin(), bytes.end())));
    }

    TEST(BinaryCodecTest, RejectsTruncatedAndOversizedLengthsBeforeChangingTheDestination) {
        const auto check = []<typename Length>() {
            Binary::Writer writer;
            EXPECT_FALSE(writer.write_string<Length>("abc", 2));
            EXPECT_TRUE(writer.data().empty());
            ASSERT_TRUE(writer.write_string<Length>("abc", 3));
            for(const auto size : {sizeof(Length) - 1, writer.data().size() - 1}) {
                Binary::Reader reader(std::span(writer.data()).first(size));
                std::string text = "keep";
                EXPECT_FALSE(reader.read_string<Length>(text, 3));
                EXPECT_EQ(text, "keep");
            }
            Binary::Reader reader(writer.data());
            std::string text = "keep";
            EXPECT_FALSE(reader.read_string<Length>(text, 2));
            EXPECT_EQ(text, "keep");
        };
        check.operator()<std::uint32_t>();
        check.operator()<std::uint64_t>();
    }

    TEST(BinaryCodecTest, HashMatchesKnownFnv1aValueAcrossChunks) {
        const std::string_view text = "hello";
        const auto bytes = std::as_bytes(std::span(text));
        EXPECT_EQ(Binary::hash_bytes({}), Binary::HASH_SEED);
        EXPECT_EQ(Binary::hash_bytes(bytes), 0xa430d84680aabd0bull);
        EXPECT_EQ(Binary::hash_bytes(bytes.subspan(2), Binary::hash_bytes(bytes.first(2))),
            Binary::hash_bytes(bytes));
    }
}
