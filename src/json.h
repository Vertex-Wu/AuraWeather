#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <cstdio>

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    bool isNull() const { return type == Null; }
    bool isObj() const { return type == Object; }
    bool isArr() const { return type == Array; }
    bool isStr() const { return type == String; }
    bool isNum() const { return type == Number; }
    const JsonValue* get(const std::string& k) const {
        if (type != Object) return nullptr;
        for (const auto& p : obj) if (p.first == k) return &p.second;
        return nullptr;
    }
    const JsonValue& at(size_t i) const { static JsonValue n; return i < arr.size() ? arr[i] : n; }
    size_t size() const { return type == Array ? arr.size() : (type == Object ? obj.size() : 0); }
    double number(double def = 0) const { return type == Number ? num : def; }
    std::string string(const std::string& def = "") const { return type == String ? str : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : def; }
};

class JsonParser {
public:
    static JsonValue parse(const std::string& text, std::string* error = nullptr) {
        JsonParser p(text);
        JsonValue v;
        p.skip();
        if (!p.value(v, 0)) {
            if (error) *error = p.err.empty() ? "JSON 解析失败" : p.err;
            return JsonValue{};
        }
        p.skip();
        if (p.pos != p.s.size()) {
            if (error) *error = "JSON 尾部存在多余内容";
            return JsonValue{};
        }
        return v;
    }

private:
    explicit JsonParser(const std::string& s) : s(s) {}
    const std::string& s;
    size_t pos = 0;
    std::string err;

    void skip() {
        while (pos < s.size()) {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') pos++;
            else break;
        }
    }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    bool value(JsonValue& out, int depth) {
        if (depth > 100) return fail("JSON 嵌套过深");
        skip();
        if (pos >= s.size()) return fail("JSON 意外结束");
        char c = s[pos];
        if (c == '{') return object(out, depth + 1);
        if (c == '[') return array(out, depth + 1);
        if (c == '"') { out.type = JsonValue::String; return string(out.str); }
        if (c == 't') { if (s.compare(pos, 4, "true") == 0) { pos += 4; out.type = JsonValue::Bool; out.b = true; return true; } return fail("无效字面量"); }
        if (c == 'f') { if (s.compare(pos, 5, "false") == 0) { pos += 5; out.type = JsonValue::Bool; out.b = false; return true; } return fail("无效字面量"); }
        if (c == 'n') { if (s.compare(pos, 4, "null") == 0) { pos += 4; out.type = JsonValue::Null; return true; } return fail("无效字面量"); }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail("JSON 无法识别的字符");
    }

    bool number(JsonValue& out) {
        size_t start = pos;
        if (s[pos] == '-') pos++;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
        if (pos < s.size() && s[pos] == '.') {
            pos++;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            pos++;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
        }
        std::string part = s.substr(start, pos - start);
        char* end = nullptr;
        double v = strtod(part.c_str(), &end);
        if (!end || *end) return fail("无效数字");
        out.type = JsonValue::Number;
        out.num = v;
        return true;
    }

    static void appendUtf8(std::string& o, unsigned cp) {
        if (cp <= 0x7F) o.push_back((char)cp);
        else if (cp <= 0x7FF) {
            o.push_back((char)(0xC0 | (cp >> 6)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            o.push_back((char)(0xE0 | (cp >> 12)));
            o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            o.push_back((char)(0xF0 | (cp >> 18)));
            o.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            o.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(unsigned& v) {
        if (pos + 4 > s.size()) return fail("JSON 转义不完整");
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = s[pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return fail("JSON 转义无效");
        }
        return true;
    }

    bool string(std::string& out) {
        out.clear();
        if (pos >= s.size() || s[pos] != '"') return fail("缺少字符串引号");
        pos++;
        while (pos < s.size()) {
            unsigned char c = (unsigned char)s[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= s.size()) return fail("JSON 转义不完整");
                char e = s[pos++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                        pos += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("JSON 无效转义");
                }
            } else if (c < 0x20) {
                return fail("JSON 字符串包含控制字符");
            } else {
                out.push_back((char)c);
            }
        }
        return fail("JSON 字符串未结束");
    }

    bool array(JsonValue& out, int depth) {
        out.type = JsonValue::Array;
        out.arr.clear();
        pos++;
        skip();
        if (pos < s.size() && s[pos] == ']') { pos++; return true; }
        while (pos < s.size()) {
            JsonValue v;
            if (!value(v, depth)) return false;
            out.arr.push_back(std::move(v));
            skip();
            if (pos >= s.size()) return fail("JSON 数组未结束");
            if (s[pos] == ',') { pos++; skip(); continue; }
            if (s[pos] == ']') { pos++; return true; }
            return fail("JSON 数组分隔符无效");
        }
        return fail("JSON 数组未结束");
    }

    bool object(JsonValue& out, int depth) {
        out.type = JsonValue::Object;
        out.obj.clear();
        pos++;
        skip();
        if (pos < s.size() && s[pos] == '}') { pos++; return true; }
        while (pos < s.size()) {
            skip();
            std::string k;
            if (!string(k)) return false;
            skip();
            if (pos >= s.size() || s[pos] != ':') return fail("JSON 对象缺少冒号");
            pos++;
            JsonValue v;
            if (!value(v, depth)) return false;
            out.obj.emplace_back(std::move(k), std::move(v));
            skip();
            if (pos >= s.size()) return fail("JSON 对象未结束");
            if (s[pos] == ',') { pos++; continue; }
            if (s[pos] == '}') { pos++; return true; }
            return fail("JSON 对象分隔符无效");
        }
        return fail("JSON 对象未结束");
    }
};

