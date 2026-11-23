#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace json {

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Variant data;

    Value() : data(nullptr) {}
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool b) : data(b) {}
    Value(int i) : data(static_cast<double>(i)) {}
    Value(int64_t i) : data(static_cast<double>(i)) {}
    Value(double d) : data(d) {}
    Value(const char* s) : data(std::string(s ? s : "")) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(Array a) : data(std::move(a)) {}
    Value(Object o) : data(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }

    std::string as_string(const std::string& def = "") const {
        if (auto* p = std::get_if<std::string>(&data)) return *p;
        if (auto* p = std::get_if<double>(&data)) {
            char buf[64];
            if (static_cast<double>(static_cast<int64_t>(*p)) == *p) {
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(*p));
            } else {
                std::snprintf(buf, sizeof(buf), "%g", *p);
            }
            return buf;
        }
        if (auto* p = std::get_if<bool>(&data)) return *p ? "true" : "false";
        return def;
    }

    int64_t as_int64(int64_t def = 0) const {
        if (auto* p = std::get_if<double>(&data)) return static_cast<int64_t>(*p);
        if (auto* p = std::get_if<std::string>(&data)) {
            try { return std::stoll(*p); } catch (...) { return def; }
        }
        return def;
    }

    const Array& as_array() const {
        static const Array empty;
        if (auto* p = std::get_if<Array>(&data)) return *p;
        return empty;
    }

    const Object& as_object() const {
        static const Object empty;
        if (auto* p = std::get_if<Object>(&data)) return *p;
        return empty;
    }

    const Value& get(const std::string& key) const {
        static const Value null_val;
        if (auto* p = std::get_if<Object>(&data)) {
            auto it = p->find(key);
            if (it != p->end()) return it->second;
        }
        return null_val;
    }
};

namespace detail {

inline void escape_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

inline void dump_into(std::string& out, const Value& v, int indent, int depth) {
    if (v.is_null()) {
        out += "null";
    } else if (v.is_bool()) {
        out += std::get<bool>(v.data) ? "true" : "false";
    } else if (v.is_number()) {
        double d = std::get<double>(v.data);
        char buf[64];
        if (static_cast<double>(static_cast<int64_t>(d)) == d) {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        } else {
            std::snprintf(buf, sizeof(buf), "%.15g", d);
        }
        out += buf;
    } else if (v.is_string()) {
        escape_string(out, std::get<std::string>(v.data));
    } else if (v.is_array()) {
        const auto& arr = v.as_array();
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) out.push_back(',');
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            dump_into(out, arr[i], indent, depth + 1);
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back(']');
    } else if (v.is_object()) {
        const auto& obj = v.as_object();
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        bool first = true;
        for (const auto& [k, val] : obj) {
            if (!first) out.push_back(',');
            first = false;
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>((depth + 1) * indent), ' ');
            }
            escape_string(out, k);
            out += (indent > 0) ? ": " : ":";
            dump_into(out, val, indent, depth + 1);
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(depth * indent), ' ');
        }
        out.push_back('}');
    }
}

class Parser {
public:
    explicit Parser(const std::string& src) : s_(src), pos_(0) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        return v;
    }

private:
    const std::string& s_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= s_.size()) throw std::runtime_error("Unexpected end of JSON");
        return s_[pos_];
    }

    char next() {
        char c = peek();
        ++pos_;
        return c;
    }

    Value parse_value() {
        skip_ws();
        char c = peek();
        if (c == '"') return Value(parse_string());
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't') { expect("true"); return Value(true); }
        if (c == 'f') { expect("false"); return Value(false); }
        if (c == 'n') { expect("null"); return Value(nullptr); }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        throw std::runtime_error("Invalid JSON value");
    }

    void expect(const char* lit) {
        while (*lit) {
            if (next() != *lit++) throw std::runtime_error("Unexpected token in JSON");
        }
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    uint32_t parse_hex4() {
        uint32_t val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = next();
            val <<= 4;
            if (c >= '0' && c <= '9') val |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') val |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val |= static_cast<uint32_t>(c - 'A' + 10);
            else throw std::runtime_error("Invalid hex escape");
        }
        return val;
    }

    std::string parse_string() {
        if (next() != '"') throw std::runtime_error("Expected '\"'");
        std::string out;
        while (true) {
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 2 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = parse_hex4();
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Value parse_number() {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
               s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' ||
               s_[pos_] == '+' || s_[pos_] == '-')) {
            ++pos_;
        }
        return Value(std::stod(s_.substr(start, pos_ - start)));
    }

    Value parse_array() {
        next(); // '['
        Array arr;
        skip_ws();
        if (peek() == ']') { next(); return Value(std::move(arr)); }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            char c = next();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("Expected ',' or ']'");
        }
        return Value(std::move(arr));
    }

    Value parse_object() {
        next(); // '{'
        Object obj;
        skip_ws();
        if (peek() == '}') { next(); return Value(std::move(obj)); }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (next() != ':') throw std::runtime_error("Expected ':'");
            obj[std::move(key)] = parse_value();
            skip_ws();
            char c = next();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("Expected ',' or '}'");
        }
        return Value(std::move(obj));
    }
};

} // namespace detail

inline Value parse(const std::string& src) {
    return detail::Parser(src).parse();
}

inline std::string dump(const Value& v, int indent = 2) {
    std::string out;
    detail::dump_into(out, v, indent, 0);
    out.push_back('\n');
    return out;
}

} // namespace json
