#include "asset/data/game_data.h"
#include "common/json.h"

#include <set>

namespace Comet {
    namespace {
        using Value = GameDataValue;
        using Fields = GameData::Fields;
        constexpr double MAX_EXACT_INTEGER = 9007199254740991.0;

        bool valid_name(std::string_view name) {
            return !name.empty() && name.size() <= 128
                   && std::ranges::none_of(
                       name, [](unsigned char c) { return c < 32 || c == 127; });
        }

        Result<Fields> read_fields(Json::Node node, const Json::Context& context) {
            auto object = context.object(node, "fields");
            if(!object)
                return Result<Fields>::failure(object.error());
            Fields result;
            for(auto entry : object.value()) {
                const auto location = "fields." + std::string(entry.key);
                if(!valid_name(entry.key) || entry.key == "id" || result.size() >= 63)
                    return Result<Fields>::failure(
                        context.error(location, "invalid field name or more than 63 fields"));
                if(auto keys = context.validate_keys(
                       entry.value, {"type", "min", "max", "asset_type"}, location);
                    !keys)
                    return Result<Fields>::failure(keys.error());
                auto type =
                    context.read_field<std::string>(entry.value, "type", "a type name", location);
                auto min = context.read_optional_field<double>(
                    entry.value, "min", "a finite number", location);
                auto max = context.read_optional_field<double>(
                    entry.value, "max", "a finite number", location);
                auto asset = context.read_optional_field<std::string>(
                    entry.value, "asset_type", "an asset type", location);
                if(!type)
                    return Result<Fields>::failure(type.error());
                if(!min)
                    return Result<Fields>::failure(min.error());
                if(!max)
                    return Result<Fields>::failure(max.error());
                if(!asset)
                    return Result<Fields>::failure(asset.error());
                GameData::Field field{type.value(), min.value(), max.value()};
                const bool numeric = field.type == "number" || field.type == "integer";
                if(!numeric && field.type != "string" && field.type != "bool"
                    && field.type != "json" && field.type != "asset")
                    return Result<Fields>::failure(context.error(location, "unknown field type"));
                if((!numeric && (field.min || field.max))
                    || (field.min && field.max && *field.min > *field.max))
                    return Result<Fields>::failure(
                        context.error(location, "invalid numeric range"));
                if(field.type == "asset") {
                    if(asset.value())
                        field.asset_type = asset_type_from_string(*asset.value());
                    if(!field.asset_type || *field.asset_type == AssetType::Unknown
                        || *field.asset_type == AssetType::Shader
                        || *field.asset_type == AssetType::Model
                        || *field.asset_type == AssetType::Scene)
                        return Result<Fields>::failure(
                            context.error(location, "expected a runtime asset_type"));
                } else if(asset.value())
                    return Result<Fields>::failure(
                        context.error(location, "asset_type requires type asset"));
                result.emplace(entry.key, std::move(field));
            }
            return Result<Fields>::success(std::move(result));
        }

        Result<Value> read_value(Json::Node node, const Json::Context& context,
            const std::string& location, size_t& remaining, unsigned depth = 0) {
            using Read = Result<Value>;
            if(!remaining || depth > 32)
                return Read::failure(
                    context.error(location, "data exceeds 100000 values or 32 nesting levels"));
            --remaining;
            switch(node.type()) {
                case Json::Kind::OBJECT: {
                    auto object = context.object(node, location);
                    if(!object)
                        return Read::failure(object.error());
                    Value::Object result;
                    for(auto field : object.value()) {
                        if(!valid_name(field.key))
                            return Read::failure(context.error(location, "invalid object key"));
                        auto value = read_value(field.value, context,
                            location + "." + std::string(field.key), remaining, depth + 1);
                        if(!value)
                            return value;
                        result.emplace(field.key, std::move(value).value());
                    }
                    return Read::success({std::move(result)});
                }
                case Json::Kind::ARRAY: {
                    Value::Array result;
                    const auto array = node.get_array().value();
                    for(auto item : array) {
                        auto value = read_value(item, context,
                            location + "[" + std::to_string(result.size()) + "]", remaining,
                            depth + 1);
                        if(!value)
                            return value;
                        result.push_back(std::move(value).value());
                    }
                    return Read::success({std::move(result)});
                }
                case Json::Kind::STRING:
                    return Read::success({std::string(node.get_string().value())});
                case Json::Kind::BOOL:
                    return Read::success({node.get_bool().value()});
                case Json::Kind::INT64:
                case Json::Kind::UINT64: {
                    auto number = context.read_scalar<int64_t>(node, location, "an exact integer");
                    if(!number || std::abs(double(number.value())) > MAX_EXACT_INTEGER)
                        return Read::failure(context.error(
                            location, "integer exceeds exact range; use a string for identities"));
                    return Read::success({number.value()});
                }
                case Json::Kind::DOUBLE: {
                    auto number = context.read_scalar<double>(node, location, "a finite number");
                    if(!number)
                        return Read::failure(number.error());
                    return Read::success({number.value()});
                }
                default:
                    return Read::failure(context.error(
                        location, "null is not supported; omit optional nested keys"));
            }
        }

        void write_value(Json::Writer& writer, const Value& value);
        void write_record(Json::Writer& writer, const Value::Object& record) {
            writer.begin_object();
            for(const auto& [key, child] : record) {
                writer.key(key);
                write_value(writer, child);
            }
            writer.end_object();
        }

        void write_value(Json::Writer& writer, const Value& value) {
            std::visit(
                [&](const auto& data) {
                    using T = std::decay_t<decltype(data)>;
                    if constexpr(std::is_same_v<T, Value::Object>) {
                        write_record(writer, data);
                    } else if constexpr(std::is_same_v<T, Value::Array>) {
                        writer.begin_array();
                        for(const auto& child : data)
                            write_value(writer, child);
                        writer.end_array();
                    } else
                        writer.value(data);
                },
                value.value);
        }

        Result<void> validate_value(const Value& value, const GameData::Field& field,
            std::vector<AssetReference>& references) {
            const auto* text = std::get_if<std::string>(&value.value);
            const auto* integer = std::get_if<int64_t>(&value.value);
            const auto* number = std::get_if<double>(&value.value);
            if(field.type == "number" || field.type == "integer") {
                if((!integer && !number) || (field.type == "integer" && !integer))
                    return Result<void>::failure("expected " + field.type);
                const double scalar = integer ? double(*integer) : *number;
                if((field.min && scalar < *field.min) || (field.max && scalar > *field.max))
                    return Result<void>::failure("number outside declared range");
            } else if(field.type == "asset") {
                if(!text || text->empty())
                    return Result<void>::failure("expected a nonempty asset identity string");
                const auto handle = AssetHandle::parse(*text);
                if(!handle)
                    return Result<void>::failure("invalid asset identity");
                references.push_back({*handle, *field.asset_type});
            } else if(field.type == "string" && !text)
                return Result<void>::failure("expected string");
            else if(field.type == "bool" && !std::holds_alternative<bool>(value.value))
                return Result<void>::failure("expected bool");
            else if(field.type == "json" && !std::holds_alternative<Value::Object>(value.value)
                    && !std::holds_alternative<Value::Array>(value.value))
                return Result<void>::failure("expected a JSON object or array");
            return Result<void>::success();
        }

        Result<void> validate_records(const Fields& fields, const GameData::Records& records,
            std::vector<AssetReference>& references, const Json::Context& context) {
            for(const auto& [id, object] : records) {
                const auto location = "rows." + id;
                if(object.size() != fields.size() + 1)
                    return Result<void>::failure(
                        context.error(location, "record fields do not match schema"));
                for(const auto& [key, field] : fields) {
                    const auto found = object.find(key);
                    if(found == object.end())
                        return Result<void>::failure(
                            context.error(location + "." + key, "missing field"));
                    if(auto valid = validate_value(found->second, field, references); !valid)
                        return Result<void>::failure(
                            context.error(location + "." + key, valid.error()));
                }
            }
            std::ranges::sort(references);
            references.erase(std::unique(references.begin(), references.end()), references.end());
            return Result<void>::success();
        }

        // RFC 4180 引号、双引号转义与多行单元格；保留字符串中的空白。
        Result<std::vector<std::vector<std::string>>> csv_rows(std::string_view text) {
            using Rows = std::vector<std::vector<std::string>>;
            if(text.starts_with("\xef\xbb\xbf"))
                text.remove_prefix(3);
            if(!Json::valid_utf8(text))
                return Result<Rows>::failure("CSV must be UTF-8");
            Rows rows;
            std::vector<std::string> row;
            size_t cursor = 0;
            while(cursor < text.size()) {
                std::string cell;
                if(text[cursor] == '"') {
                    ++cursor;
                    bool closed = false;
                    while(cursor < text.size()) {
                        const char c = text[cursor++];
                        if(c != '"') {
                            cell += c;
                            continue;
                        }
                        if(cursor < text.size() && text[cursor] == '"') {
                            cell += '"';
                            ++cursor;
                        } else {
                            closed = true;
                            break;
                        }
                    }
                    if(!closed)
                        return Result<Rows>::failure("CSV has an unclosed quoted cell");
                } else {
                    while(cursor < text.size() && text[cursor] != ',' && text[cursor] != '\r'
                          && text[cursor] != '\n') {
                        if(text[cursor] == '"')
                            return Result<Rows>::failure("CSV quote must start a cell");
                        cell += text[cursor++];
                    }
                }
                row.push_back(std::move(cell));
                if(row.size() > 64 || rows.size() > 10000)
                    return Result<Rows>::failure("CSV exceeds row or column limit");
                if(cursor == text.size()) {
                    rows.push_back(std::move(row));
                    break;
                }
                const char separator = text[cursor++];
                if(separator == ',') {
                    if(cursor == text.size()) {
                        row.emplace_back();
                        rows.push_back(std::move(row));
                    }
                    continue;
                }
                if(separator != '\r' && separator != '\n')
                    return Result<Rows>::failure("Unexpected text after CSV quote");
                if(separator == '\r' && cursor < text.size() && text[cursor] == '\n')
                    ++cursor;
                rows.push_back(std::move(row));
                row.clear();
            }
            return Result<Rows>::success(std::move(rows));
        }
    }

    Result<GameData> GameData::parse_json(std::string_view text, std::string_view source) {
        if(text.size() > MAX_SOURCE_BYTES)
            return Result<GameData>::failure("Data source exceeds 16 MiB");
        return Json::deserialize<GameData>("game data", text, source,
            [](Json::Node root, const Json::Context& context) -> Result<GameData> {
                using Read = Result<GameData>;
                if(auto keys = context.validate_keys(root, {"fields", "rows"}); !keys)
                    return Read::failure(keys.error());
                auto fields_node = context.required_child(root, "fields");
                auto rows_node = context.required_child(root, "rows");
                if(!fields_node)
                    return Read::failure(fields_node.error());
                if(!rows_node)
                    return Read::failure(rows_node.error());
                auto fields = read_fields(fields_node.value(), context);
                auto rows = context.array(rows_node.value(), "rows");
                if(!fields)
                    return Read::failure(fields.error());
                if(!rows)
                    return Read::failure(rows.error());
                if(rows.value().size() > 10000)
                    return Read::failure("Data exceeds 10000 records");
                GameData result;
                result.m_fields = std::move(fields).value();
                size_t remaining = 100000;
                for(auto row : rows.value()) {
                    const auto location = "rows[" + std::to_string(result.m_records.size()) + "]";
                    auto parsed = read_value(row, context, location, remaining);
                    if(!parsed)
                        return Read::failure(parsed.error());
                    auto* object = std::get_if<Value::Object>(&parsed.value().value);
                    if(!object)
                        return Read::failure(context.error(location, "expected a record object"));
                    const auto id = object->find("id");
                    const auto* name =
                        id == object->end() ? nullptr : std::get_if<std::string>(&id->second.value);
                    if(!name || !valid_name(*name))
                        return Read::failure(
                            context.error(location, "expected a nonempty string id"));
                    const auto identity = *name;
                    if(!result.m_records.emplace(identity, std::move(*object)).second)
                        return Read::failure(
                            context.error(location, "duplicate id '" + identity + "'"));
                }
                if(auto valid = validate_records(
                       result.m_fields, result.m_records, result.m_references, context);
                    !valid)
                    return Read::failure(valid.error());
                return Read::success(std::move(result));
            });
    }

    Result<GameData> GameData::parse_csv(
        std::string_view text, std::string_view schema, std::string_view source) {
        using Read = Result<GameData>;
        if(text.size() > MAX_SOURCE_BYTES || schema.size() > MAX_SOURCE_BYTES)
            return Read::failure("Data source exceeds 16 MiB");
        auto fields = Json::deserialize<Fields>("CSV schema", schema, source,
            [](Json::Node root, const Json::Context& context) -> Result<Fields> {
                if(auto keys = context.validate_keys(root, {"fields"}); !keys)
                    return Result<Fields>::failure(keys.error());
                auto node = context.required_child(root, "fields");
                if(!node)
                    return Result<Fields>::failure(node.error());
                return read_fields(node.value(), context);
            });
        if(!fields)
            return Read::failure(fields.error());
        auto rows = csv_rows(text);
        if(!rows)
            return Read::failure(std::string(source) + ": " + rows.error());
        if(rows.value().empty())
            return Read::failure("CSV header is missing");
        const auto& header = rows.value().front();
        std::set<std::string> names;
        for(const auto& key : header)
            if(!names.insert(key).second || (key != "id" && !fields.value().contains(key)))
                return Read::failure("CSV header contains duplicate or unknown field: " + key);
        if(!names.contains("id") || header.size() != fields.value().size() + 1)
            return Read::failure("CSV header must contain id and all declared fields");
        GameData result;
        result.m_fields = std::move(fields).value();
        size_t remaining = 100000;
        for(size_t index = 1; index < rows.value().size(); ++index) {
            const auto& row = rows.value()[index];
            if(row.size() != header.size())
                return Read::failure(
                    "CSV record " + std::to_string(index) + " has the wrong column count");
            if(!remaining)
                return Read::failure("CSV exceeds 100000 values");
            --remaining;
            Value::Object record;
            for(size_t column = 0; column < header.size(); ++column) {
                const auto& key = header[column];
                const auto& cell = row[column];
                if(key == "id" || result.m_fields.at(key).type == "string"
                    || result.m_fields.at(key).type == "asset") {
                    if(!remaining)
                        return Read::failure("CSV exceeds 100000 values");
                    --remaining;
                    record.emplace(key, Value{cell});
                } else {
                    auto value = Json::deserialize<Value>("CSV cell", cell, source,
                        [&](Json::Node node, const Json::Context& context) {
                            return read_value(node, context,
                                "record " + std::to_string(index) + "." + key, remaining, 1);
                        });
                    if(!value)
                        return Read::failure(value.error());
                    record.emplace(key, std::move(value).value());
                }
            }
            const auto id = std::get<std::string>(record.at("id").value);
            if(!valid_name(id) || !result.m_records.emplace(id, std::move(record)).second)
                return Read::failure("CSV has invalid or duplicate id: " + id);
        }
        const Json::Context context("CSV data", source);
        if(auto valid =
                validate_records(result.m_fields, result.m_records, result.m_references, context);
            !valid)
            return Read::failure(valid.error());
        return Read::success(std::move(result));
    }

    std::filesystem::path GameData::schema_path(std::filesystem::path csv) {
        csv += ".schema.json";
        return csv;
    }

    Result<GameData> GameData::load(const std::filesystem::path& path) {
        auto bytes = read_binary_file(path, MAX_SOURCE_BYTES);
        if(!bytes)
            return Result<GameData>::failure(bytes.error());
        const std::string text(
            reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
        if(source_extension(path) != ".csv")
            return parse_json(text, path.string());
        auto schema = read_binary_file(schema_path(path), MAX_SOURCE_BYTES);
        if(!schema)
            return Result<GameData>::failure(schema.error());
        return parse_csv(text,
            std::string_view(
                reinterpret_cast<const char*>(schema.value().data()), schema.value().size()),
            path.string());
    }

    const Value::Object* GameData::find(std::string_view id) const {
        const auto found = m_records.find(id);
        return found == m_records.end() ? nullptr : &found->second;
    }

    Result<std::string> GameData::serialize() const {
        Json::Writer writer;
        writer.begin_object();
        writer.key("fields");
        writer.begin_object();
        for(const auto& [key, field] : m_fields) {
            writer.key(key);
            writer.begin_object();
            writer.field("type", field.type);
            if(field.min)
                writer.field("min", *field.min);
            if(field.max)
                writer.field("max", *field.max);
            if(field.asset_type)
                writer.field("asset_type", to_string(*field.asset_type));
            writer.end_object();
        }
        writer.end_object();
        writer.key("rows");
        writer.begin_array();
        for(const auto& [id, record] : m_records)
            write_record(writer, record);
        writer.end_array();
        writer.end_object();
        auto encoded = std::move(writer).finish();
        if(encoded && encoded.value().size() > MAX_SOURCE_BYTES)
            return Result<std::string>::failure("Data product exceeds 16 MiB");
        return encoded;
    }
}
