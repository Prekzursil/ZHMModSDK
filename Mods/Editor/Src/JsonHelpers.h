#pragma once

#include <Glacier/ZMath.h>
#include <Glacier/ZString.h>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>

// Note: this header previously used simdjson internals (fast_itoa,
// internal::to_chars, internal::mini_formatter), which newer simdjson
// releases removed. Use the standard library instead.

inline std::string write_json(int64_t p_Value) {
    char s_NumberBuffer[24];
    const auto s_Result = std::to_chars(s_NumberBuffer, s_NumberBuffer + sizeof(s_NumberBuffer), p_Value);
    return std::string(s_NumberBuffer, s_Result.ptr);
}

inline std::string write_json(uint64_t p_Value) {
    char s_NumberBuffer[24];
    const auto s_Result = std::to_chars(s_NumberBuffer, s_NumberBuffer + sizeof(s_NumberBuffer), p_Value);
    return std::string(s_NumberBuffer, s_Result.ptr);
}

inline std::string write_json(int8_t p_Value) {
    return write_json(static_cast<int64_t>(p_Value));
}

inline std::string write_json(uint8_t p_Value) {
    return write_json(static_cast<uint64_t>(p_Value));
}

inline std::string write_json(int16_t p_Value) {
    return write_json(static_cast<int64_t>(p_Value));
}

inline std::string write_json(uint16_t p_Value) {
    return write_json(static_cast<uint64_t>(p_Value));
}

inline std::string write_json(int32_t p_Value) {
    return write_json(static_cast<int64_t>(p_Value));
}

inline std::string write_json(uint32_t p_Value) {
    return write_json(static_cast<uint64_t>(p_Value));
}

inline std::string write_json(double p_Value) {
    char s_NumberBuffer[32];
    // General format, shortest round-trippable representation.
    const auto s_Result = std::to_chars(s_NumberBuffer, s_NumberBuffer + sizeof(s_NumberBuffer), p_Value);
    return std::string(s_NumberBuffer, s_Result.ptr);
}

inline std::string write_json(float p_Value) {
    return write_json(static_cast<double>(p_Value));
}

inline std::string write_json(std::string_view p_Value) {
    // JSON string with quotes and the mandatory escapes.
    std::string s_Result;
    s_Result.reserve(p_Value.size() + 2);
    s_Result += '"';

    for (const char s_Char : p_Value) {
        switch (s_Char) {
        case '"': s_Result += "\\\""; break;
        case '\\': s_Result += "\\\\"; break;
        case '\b': s_Result += "\\b"; break;
        case '\f': s_Result += "\\f"; break;
        case '\n': s_Result += "\\n"; break;
        case '\r': s_Result += "\\r"; break;
        case '\t': s_Result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(s_Char) < 0x20) {
                char s_Escape[7];
                std::snprintf(s_Escape, sizeof(s_Escape), "\\u%04x", s_Char);
                s_Result += s_Escape;
            }
            else {
                s_Result += s_Char;
            }
            break;
        }
    }

    s_Result += '"';
    return s_Result;
}

inline std::string write_json(const char* p_Value) {
    return write_json(std::string_view(p_Value));
}

inline std::string write_json(const std::string& p_Value) {
    return write_json(std::string_view(p_Value));
}

inline std::string write_json(const ZString& p_Value) {
    return write_json(std::string_view(p_Value));
}

inline std::string write_json(bool p_Value) {
    return p_Value ? "true" : "false";
}
