#include "common/serialization/parameters_json.h"

#include <gtest/gtest.h>

#include <limits>

namespace Comet::Tests {
    namespace {
        Result<ParameterMap> read_parameters(
            std::string_view contents, Json::ParameterReferenceReader references = nullptr) {
            return Json::deserialize<ParameterMap>("parameters", contents, "test.json",
                [references](Json::Node node, const Json::Context& context) {
                    return Json::read_parameters(node, context, "values", references);
                });
        }

        Result<std::string> write_parameters(
            const ParameterMap& parameters, Json::ParameterReferenceWriter references = nullptr) {
            return Json::serialize("parameters", parameters,
                [references](const ParameterMap& values, const Json::Context& context,
                    Json::Writer& writer) {
                    return Json::write_parameters(writer, values, context, "values", references);
                });
        }
    }

    TEST(ParametersJsonTest, RoundTripPreservesScalarAndVectorTypesAndEscapedText) {
        const ParameterMap parameters{{"enabled", true}, {"speed", 1.25f},
            {"offset", Math::Vec3(1, 2, 3)}, {"color", Math::Vec4(1, 2, 3, 4)},
            {"名称", std::string("角色\n\"引用\"")}};
        const auto encoded = write_parameters(parameters);
        ASSERT_TRUE(encoded) << encoded.error();
        const auto decoded = read_parameters(encoded.value());
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value(), parameters);
        EXPECT_TRUE(read_parameters("{}"));
    }

    TEST(ParametersJsonTest, RejectsDuplicateNamesUnsupportedTypesAndInvalidVectors) {
        for(const auto contents : {R"({"value":1,"value":2})", R"({"value":1,"\u0076alue":2})",
                R"({"value":null})", R"({"value":{}})", R"({"value":[]})", R"({"value":[1,2]})",
                R"({"value":[1,2,3,4,5]})", R"({"value":[1,"2",3]})", R"({"value":[1,2,true,4]})",
                R"({"value":1e100})", R"({"value":[1,2,1e100]})", R"({"value":[1,2,3,1e100]})",
                R"({"":1})", R"({"a\u0000b":1})"}) {
            const auto decoded = read_parameters(contents);
            EXPECT_FALSE(decoded) << contents;
            if(!decoded)
                EXPECT_NE(decoded.error().find("values"), std::string::npos) << decoded.error();
        }
    }

    TEST(ParametersJsonTest, EnforcesSharedCapacityNameAndStringLimitsAtBothBoundaries) {
        ParameterMap parameters;
        for(int index = 0; index < 128; ++index)
            parameters.emplace("key" + std::to_string(index), float(index));
        const auto encoded = write_parameters(parameters);
        ASSERT_TRUE(encoded);
        ASSERT_TRUE(read_parameters(encoded.value()));
        auto too_many = encoded.value();
        too_many.insert(too_many.find_last_of('}'), ",\"extra\":1");
        EXPECT_FALSE(read_parameters(too_many));
        parameters.emplace("extra", 1.f);
        EXPECT_FALSE(write_parameters(parameters));
        parameters = {{std::string(128, 'n'), std::string(4096, 'v')}};
        ASSERT_TRUE(write_parameters(parameters));
        EXPECT_TRUE(read_parameters(write_parameters(parameters).value()));
        for(const auto& contents : {"{\"" + std::string(129, 'n') + "\":1}",
                "{\"value\":\"" + std::string(4097, 'v') + "\"}"})
            EXPECT_FALSE(read_parameters(contents));
        EXPECT_FALSE(write_parameters({{"", 1.f}}));
        EXPECT_FALSE(write_parameters({{"value", std::numeric_limits<float>::infinity()}}));
        EXPECT_FALSE(
            write_parameters({{"value", Math::Vec4(std::numeric_limits<float>::quiet_NaN())}}));
    }

    TEST(ParametersJsonTest, ReferenceShapeIsProvidedByCallerAndRejectedWithoutItsCodec) {
        const auto uuid = Uuid::generate();
        const ParameterMap parameters{{"target", uuid}};
        const auto write_reference = +[](Json::Writer& writer, Uuid value) {
            writer.begin_object();
            writer.field("identity", value.to_string());
            writer.end_object();
        };
        const auto read_reference = +[](Json::Node node, const Json::Context& context,
                                         std::string_view location) -> Result<Uuid> {
            auto text = context.read_field<std::string>(node, "identity", "a UUID", location);
            if(!text)
                return Result<Uuid>::failure(text.error());
            const auto uuid = Uuid::parse(text.value());
            if(!uuid)
                return Result<Uuid>::failure(context.error(location, "invalid UUID"));
            return Result<Uuid>::success(*uuid);
        };
        EXPECT_FALSE(write_parameters(parameters));
        const auto encoded = write_parameters(parameters, write_reference);
        ASSERT_TRUE(encoded);
        EXPECT_FALSE(read_parameters(encoded.value()));
        const auto decoded = read_parameters(encoded.value(), read_reference);
        ASSERT_TRUE(decoded) << decoded.error();
        EXPECT_EQ(decoded.value(), parameters);
    }
}
