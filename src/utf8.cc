#include "utf8.h"

#include <stdexcept>

namespace seum::utf8 {

std::u32string decode(std::string_view bytes) {
    std::u32string out;
    out.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size();) {
        unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        char32_t cp = 0;
        std::size_t need = 0;
        if (b0 < 0x80) {
            cp = b0;
            need = 0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            need = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            need = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            need = 3;
        } else {
            throw std::runtime_error("UTF-8: 잘못된 시작 바이트");
        }
        if (i + need >= bytes.size()) {
            throw std::runtime_error("UTF-8: 잘린 시퀀스");
        }
        for (std::size_t k = 1; k <= need; ++k) {
            unsigned char b = static_cast<unsigned char>(bytes[i + k]);
            if ((b & 0xC0) != 0x80) {
                throw std::runtime_error("UTF-8: 잘못된 후속 바이트");
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        // Overlong-encoding 거부
        if (need == 1 && cp < 0x80) {
            throw std::runtime_error("UTF-8: overlong 2바이트");
        }
        if (need == 2 && cp < 0x800) {
            throw std::runtime_error("UTF-8: overlong 3바이트");
        }
        if (need == 3 && cp < 0x10000) {
            throw std::runtime_error("UTF-8: overlong 4바이트");
        }
        // 서로게이트 거부
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            throw std::runtime_error("UTF-8: 서로게이트 코드포인트");
        }
        if (cp > 0x10FFFF) {
            throw std::runtime_error("UTF-8: 최대 코드포인트 초과");
        }
        out.push_back(cp);
        i += need + 1;
    }
    return out;
}

void encode_one(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            throw std::runtime_error("UTF-8 인코드: 서로게이트");
        }
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        throw std::runtime_error("UTF-8 인코드: 최대 초과");
    }
}

std::string encode(std::u32string_view codepoints) {
    std::string out;
    out.reserve(codepoints.size() * 2);
    for (char32_t cp : codepoints) {
        encode_one(cp, out);
    }
    return out;
}

bool is_digit(char32_t cp) {
    return cp >= U'0' && cp <= U'9';
}

bool is_whitespace(char32_t cp) {
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r';
}

static bool is_hangul(char32_t cp) {
    return (cp >= 0xAC00 && cp <= 0xD7A3)    // 한글 음절
        || (cp >= 0x1100 && cp <= 0x11FF)    // 자모
        || (cp >= 0x3130 && cp <= 0x318F)    // 호환 자모
        || (cp >= 0xA960 && cp <= 0xA97F)    // 확장 A
        || (cp >= 0xD7B0 && cp <= 0xD7FF);   // 확장 B
}

bool is_identifier_start(char32_t cp) {
    if (cp == U'_') return true;
    if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z')) return true;
    return is_hangul(cp);
}

bool is_identifier_char(char32_t cp) {
    return is_identifier_start(cp) || is_digit(cp);
}

}  // namespace seum::utf8
