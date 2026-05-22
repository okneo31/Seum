#pragma once

#include "common.h"

namespace seum::utf8 {

// Decode a UTF-8 byte sequence to UTF-32. Throws std::runtime_error on invalid
// sequences (overlong, surrogate code points, codepoints > U+10FFFF, truncated).
std::u32string decode(std::string_view bytes);

// Encode UTF-32 string to UTF-8.
std::string encode(std::u32string_view codepoints);

// Encode a single codepoint to UTF-8 (appended to `out`).
void encode_one(char32_t cp, std::string& out);

// True if `cp` may appear as part of a Seum identifier:
//   - ASCII letter / underscore
//   - ASCII digit (but not as first character — caller's responsibility)
//   - Hangul syllable (U+AC00..U+D7A3)
//   - Hangul jamo blocks (U+1100..U+11FF, U+3130..U+318F, U+A960..U+A97F, U+D7B0..U+D7FF)
bool is_identifier_char(char32_t cp);
bool is_identifier_start(char32_t cp);
bool is_digit(char32_t cp);
bool is_whitespace(char32_t cp);

}  // namespace seum::utf8
