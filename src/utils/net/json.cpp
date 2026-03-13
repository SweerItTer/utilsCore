/*
 * @FilePath: /src/utils/net/json.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Minimal recursive JSON parser used by utils::net runtime config
 */

#include "net/json.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace utils {
namespace net {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& outValue, std::string& error) {
        try {
            skipSpaces();
            outValue = parseValue();
            skipSpaces();
            if (!eof()) {
                error = makeError("Unexpected trailing characters");
                return false;
            }
            return true;
        } catch (const std::runtime_error& ex) {
            error = ex.what();
            return false;
        }
    }

private:
    JsonValue parseValue() {
        if (eof()) {
            throw std::runtime_error(makeError("Unexpected end of JSON"));
        }

        const char ch = peek();
        if (ch == '"') return JsonValue(parseString());
        if (ch == '{') return parseObject();
        if (ch == '[') return parseArray();
        if (ch == 't') return parseLiteral("true", JsonValue(true));
        if (ch == 'f') return parseLiteral("false", JsonValue(false));
        if (ch == 'n') return parseLiteral("null", JsonValue(nullptr));
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return JsonValue(parseNumber());
        }

        throw std::runtime_error(makeError("Invalid JSON value"));
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue::Object objectValue;
        skipSpaces();
        if (consume('}')) {
            return JsonValue(std::move(objectValue));
        }

        while (true) {
            skipSpaces();
            if (peek() != '"') {
                throw std::runtime_error(makeError("Object key must be a string"));
            }
            std::string key = parseString();
            skipSpaces();
            expect(':');
            skipSpaces();
            objectValue.entries.emplace(std::move(key), parseValue());
            skipSpaces();
            if (consume('}')) break;
            expect(',');
            skipSpaces();
        }
        return JsonValue(std::move(objectValue));
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue::Array arrayValue;
        skipSpaces();
        if (consume(']')) {
            return JsonValue(std::move(arrayValue));
        }

        while (true) {
            skipSpaces();
            arrayValue.items.emplace_back(parseValue());
            skipSpaces();
            if (consume(']')) break;
            expect(',');
            skipSpaces();
        }
        return JsonValue(std::move(arrayValue));
    }

    JsonValue parseLiteral(const char* literal, JsonValue value) {
        while (*literal != '\0') {
            expect(*literal);
            ++literal;
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (!eof()) {
            const char ch = next();
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                if (eof()) {
                    throw std::runtime_error(makeError("Invalid string escape"));
                }
                const char escaped = next();
                switch (escaped) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        // v1 only supports BMP escapes that fit into UTF-8 bytes for ASCII range.
                        std::string hex;
                        for (int i = 0; i < 4; ++i) {
                            if (eof()) {
                                throw std::runtime_error(makeError("Incomplete unicode escape"));
                            }
                            hex.push_back(next());
                        }
                        const long codePoint = std::strtol(hex.c_str(), nullptr, 16);
                        if (codePoint < 0x80) {
                            out.push_back(static_cast<char>(codePoint));
                        } else if (codePoint < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                        }
                        break;
                    }
                    default:
                        throw std::runtime_error(makeError("Unsupported string escape"));
                }
                continue;
            }
            out.push_back(ch);
        }
        throw std::runtime_error(makeError("Unterminated string"));
    }

    double parseNumber() {
        const size_t start = pos_;
        if (peek() == '-') ++pos_;
        consumeDigits();
        if (!eof() && peek() == '.') {
            ++pos_;
            consumeDigits();
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            consumeDigits();
        }

        const std::string token = text_.substr(start, pos_ - start);
        errno = 0;
        char* endPtr = nullptr;
        const double value = std::strtod(token.c_str(), &endPtr);
        if (errno != 0 || endPtr == token.c_str() || *endPtr != '\0') {
            throw std::runtime_error(makeError("Invalid number"));
        }
        return value;
    }

    void consumeDigits() {
        if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error(makeError("Expected digit"));
        }
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    void skipSpaces() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (!eof() && peek() == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (eof() || peek() != expected) {
            std::ostringstream stream;
            stream << "Expected '" << expected << "'";
            throw std::runtime_error(makeError(stream.str()));
        }
        ++pos_;
    }

    bool eof() const {
        return pos_ >= text_.size();
    }

    char peek() const {
        return text_[pos_];
    }

    char next() {
        return text_[pos_++];
    }

    std::string makeError(const std::string& message) const {
        std::ostringstream stream;
        stream << "JSON parse error at offset " << pos_ << ": " << message;
        return stream.str();
    }

    const std::string& text_;
    size_t pos_{0};
};

std::string escapeString(const std::string& input) {
    std::ostringstream stream;
    for (const char ch : input) {
        switch (ch) {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    stream << "\\u" << std::uppercase << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch))
                           << std::nouppercase << std::dec;
                } else {
                    stream << ch;
                }
                break;
        }
    }
    return stream.str();
}

} // namespace

JsonValue::JsonValue() = default;
JsonValue::JsonValue(std::nullptr_t) : JsonValue() {}
JsonValue::JsonValue(bool value) : type_(Type::Bool), boolValue_(value) {}
JsonValue::JsonValue(double value) : type_(Type::Number), numberValue_(value) {}
JsonValue::JsonValue(int value) : JsonValue(static_cast<double>(value)) {}
JsonValue::JsonValue(const char* value) : JsonValue(std::string(value ? value : "")) {}
JsonValue::JsonValue(std::string value) : type_(Type::String), stringValue_(std::move(value)) {}
JsonValue::JsonValue(Array value)
    : type_(Type::Array)
    , arrayValue_(std::make_shared<Array>(std::move(value))) {}

JsonValue::JsonValue(Object value)
    : type_(Type::Object)
    , objectValue_(std::make_shared<Object>(std::move(value))) {}

JsonValue JsonValue::array() {
    return JsonValue(Array{});
}

JsonValue JsonValue::object() {
    return JsonValue(Object{});
}

bool JsonValue::asBool(bool defaultValue) const {
    return isBool() ? boolValue_ : defaultValue;
}

double JsonValue::asNumber(double defaultValue) const {
    return isNumber() ? numberValue_ : defaultValue;
}

int JsonValue::asInt(int defaultValue) const {
    return isNumber() ? static_cast<int>(numberValue_) : defaultValue;
}

const std::string& JsonValue::asString() const {
    static const std::string empty;
    return isString() ? stringValue_ : empty;
}

const JsonValue::Array& JsonValue::asArray() const {
    static const Array empty;
    return (isArray() && arrayValue_) ? *arrayValue_ : empty;
}

const JsonValue::Object& JsonValue::asObject() const {
    static const Object empty;
    return (isObject() && objectValue_) ? *objectValue_ : empty;
}

std::string JsonValue::stringOr(std::string defaultValue) const {
    return isString() ? stringValue_ : std::move(defaultValue);
}

const JsonValue* JsonValue::find(const std::string& key) const {
    if (!isObject()) return nullptr;
    const auto it = objectValue_->entries.find(key);
    return (it != objectValue_->entries.end()) ? &it->second : nullptr;
}

JsonValue* JsonValue::find(const std::string& key) {
    if (!isObject()) return nullptr;
    const auto it = objectValue_->entries.find(key);
    return (it != objectValue_->entries.end()) ? &it->second : nullptr;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static const JsonValue nullValue;
    const JsonValue* found = find(key);
    return found ? *found : nullValue;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (!isObject()) {
        type_ = Type::Object;
        objectValue_ = std::make_shared<Object>();
        arrayValue_.reset();
        stringValue_.clear();
    }
    if (!objectValue_) {
        objectValue_ = std::make_shared<Object>();
    }
    return objectValue_->entries[key];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    static const JsonValue nullValue;
    if (!isArray() || !arrayValue_ || index >= arrayValue_->items.size()) return nullValue;
    return arrayValue_->items[index];
}

JsonValue& JsonValue::operator[](size_t index) {
    if (!isArray()) {
        type_ = Type::Array;
        arrayValue_ = std::make_shared<Array>();
        objectValue_.reset();
        stringValue_.clear();
    }
    if (!arrayValue_) {
        arrayValue_ = std::make_shared<Array>();
    }
    if (index >= arrayValue_->items.size()) {
        arrayValue_->items.resize(index + 1);
    }
    return arrayValue_->items[index];
}

size_t JsonValue::size() const {
    if (isArray() && arrayValue_) return arrayValue_->items.size();
    if (isObject() && objectValue_) return objectValue_->entries.size();
    return 0;
}

bool JsonValue::parse(const std::string& text, JsonValue& outValue, std::string& error) {
    Parser parser(text);
    return parser.parse(outValue, error);
}

bool JsonValue::parseFile(const std::string& path, JsonValue& outValue, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Failed to open JSON file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str(), outValue, error);
}

std::string JsonValue::stringify() const {
    std::ostringstream stream;
    switch (type_) {
        case Type::Null:
            stream << "null";
            break;
        case Type::Bool:
            stream << (boolValue_ ? "true" : "false");
            break;
        case Type::Number:
            stream << std::setprecision(15) << numberValue_;
            break;
        case Type::String:
            stream << '"' << escapeString(stringValue_) << '"';
            break;
        case Type::Array: {
            stream << '[';
            static const std::vector<JsonValue> emptyItems;
            const auto& items = arrayValue_ ? arrayValue_->items : emptyItems;
            for (size_t i = 0; i < items.size(); ++i) {
                if (i != 0) stream << ',';
                stream << items[i].stringify();
            }
            stream << ']';
            break;
        }
        case Type::Object: {
            stream << '{';
            bool first = true;
            const auto* entries = objectValue_ ? &objectValue_->entries : nullptr;
            if (!entries) {
                stream << '}';
                break;
            }
            for (const auto& entry : *entries) {
                if (!first) stream << ',';
                first = false;
                stream << '"' << escapeString(entry.first) << "\":" << entry.second.stringify();
            }
            stream << '}';
            break;
        }
    }
    return stream.str();
}

} // namespace net
} // namespace utils
