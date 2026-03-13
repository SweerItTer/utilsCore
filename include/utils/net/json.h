/*
 * @FilePath: /include/utils/net/json.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Minimal JSON value and parser for utils::net runtime configuration
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace utils {
namespace net {

class JsonValue {
public:
    enum class Type : uint8_t {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    struct Array;
    struct Object;

    JsonValue();
    JsonValue(std::nullptr_t);
    JsonValue(bool value);
    JsonValue(double value);
    JsonValue(int value);
    JsonValue(const char* value);
    JsonValue(std::string value);
    JsonValue(Array value);
    JsonValue(Object value);

    static JsonValue array();
    static JsonValue object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool defaultValue = false) const;
    double asNumber(double defaultValue = 0.0) const;
    int asInt(int defaultValue = 0) const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    std::string stringOr(std::string defaultValue = "") const;

    const JsonValue* find(const std::string& key) const;
    JsonValue* find(const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    JsonValue& operator[](const std::string& key);

    const JsonValue& operator[](size_t index) const;
    JsonValue& operator[](size_t index);

    size_t size() const;

    // 解析 JSON 文本。失败时返回 false 并写入 error。
    static bool parse(const std::string& text, JsonValue& outValue, std::string& error);
    // 从文件中加载并解析 JSON。
    static bool parseFile(const std::string& path, JsonValue& outValue, std::string& error);
    // 以紧凑 JSON 形式序列化。
    std::string stringify() const;

private:
    Type type_{Type::Null};
    bool boolValue_{false};
    double numberValue_{0.0};
    std::string stringValue_;
    std::shared_ptr<Array> arrayValue_;
    std::shared_ptr<Object> objectValue_;
};

struct JsonValue::Array {
    std::vector<JsonValue> items;
};

struct JsonValue::Object {
    std::unordered_map<std::string, JsonValue> entries;
};

} // namespace net
} // namespace utils
