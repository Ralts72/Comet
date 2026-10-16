#pragma once

#include "asset/content_fwd.h"

#include "asset/reference.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Comet {
    struct GameDataValue {
        using Object = std::map<std::string, GameDataValue, std::less<>>;
        using Array = std::vector<GameDataValue>;
        std::variant<bool, int64_t, double, std::string, Array, Object> value;
    };

    // 项目的只读基础数值；角色当前属性、装备与 Buff 仍归运行实例。
    class COMET_API GameData {
    public:
        struct Field {
            std::string type;
            std::optional<double> min, max;
            std::optional<AssetType> asset_type;
        };
        using Fields = std::map<std::string, Field, std::less<>>;
        using Records = std::map<std::string, GameDataValue::Object, std::less<>>;
        static constexpr size_t MAX_SOURCE_BYTES = 16 * 1024 * 1024;

        [[nodiscard]] static Result<GameData> load(const std::filesystem::path& path);
        [[nodiscard]] static Result<GameData> parse_json(
            std::string_view text, std::string_view source = "<memory>");
        [[nodiscard]] static Result<GameData> parse_csv(
            std::string_view text, std::string_view schema, std::string_view source = "<memory>");
        [[nodiscard]] static std::filesystem::path schema_path(std::filesystem::path csv);
        [[nodiscard]] Result<std::string> serialize() const;
        [[nodiscard]] const GameDataValue::Object* find(std::string_view id) const;
        [[nodiscard]] const Fields& fields() const { return m_fields; }
        [[nodiscard]] const Records& records() const { return m_records; }
        [[nodiscard]] const std::vector<AssetReference>& references() const { return m_references; }

    private:
        Fields m_fields;
        Records m_records;
        std::vector<AssetReference> m_references;
    };
}
