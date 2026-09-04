#include "scene/entity_uuid.h"

#include <gtest/gtest.h>

#include <unordered_set>

namespace Comet::Tests {
    namespace {
        constexpr EntityUuid::Bytes SAMPLE_UUID_BYTES{0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b,
            0x41, 0xd4, 0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00};
        constexpr std::string_view SAMPLE_UUID_TEXT =
            "550e8400-e29b-41d4-a716-446655440000";
    }

    TEST(EntityUuidTest, DefaultValueIsInvalid) {
        const EntityUuid uuid;

        EXPECT_FALSE(uuid);
        EXPECT_EQ(uuid, INVALID_ENTITY_UUID);
        EXPECT_EQ(uuid.to_string(), "00000000-0000-0000-0000-000000000000");
    }

    TEST(EntityUuidTest, GeneratesVersion4Uuid) {
        const EntityUuid uuid = EntityUuid::generate();

        ASSERT_TRUE(uuid);
        EXPECT_EQ(uuid.bytes()[6] >> 4U, 4U);
        EXPECT_EQ(uuid.bytes()[8] & 0xc0U, 0x80U);
        EXPECT_EQ(EntityUuid::parse(uuid.to_string()), uuid);
    }

    TEST(EntityUuidTest, ParsesAndFormatsCanonicalText) {
        const auto uuid = EntityUuid::parse(SAMPLE_UUID_TEXT);

        ASSERT_TRUE(uuid.has_value());
        EXPECT_EQ(uuid->bytes(), SAMPLE_UUID_BYTES);
        EXPECT_EQ(uuid->to_string(), SAMPLE_UUID_TEXT);
        EXPECT_EQ(EntityUuid::parse("550E8400-E29B-41D4-A716-446655440000"), uuid);
    }

    TEST(EntityUuidTest, RejectsMalformedText) {
        EXPECT_FALSE(EntityUuid::parse(""));
        EXPECT_FALSE(EntityUuid::parse("550e8400e29b41d4a716446655440000"));
        EXPECT_FALSE(EntityUuid::parse("550e8400-e29b-41d4-a716-44665544000z"));
        EXPECT_FALSE(EntityUuid::parse("550e8400_e29b-41d4-a716-446655440000"));
    }

    TEST(EntityUuidTest, SupportsHashBasedLookup) {
        const EntityUuid uuid(SAMPLE_UUID_BYTES);
        std::unordered_set<EntityUuid> values;

        EXPECT_TRUE(values.insert(uuid).second);
        EXPECT_FALSE(values.insert(EntityUuid(SAMPLE_UUID_BYTES)).second);
        EXPECT_TRUE(values.contains(uuid));
    }
}
