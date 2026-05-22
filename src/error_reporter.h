#pragma once

#include "common.h"

#include <stdexcept>

namespace seum {

// 세움 코드 처리 중 발생한 오류. 항상 위치 정보를 동반한다.
class SeumError : public std::runtime_error {
public:
    SeumError(std::string message, Position pos)
        : std::runtime_error(std::move(message)), pos_(pos) {}

    Position position() const { return pos_; }

private:
    Position pos_;
};

[[noreturn]] void raise(Position pos, const std::string& message_utf8);
[[noreturn]] void raise(Position pos, const std::u32string& message);

// Pretty-print an error to stderr (in UTF-8). Adds the source file name and line
// context if provided.
void report(const SeumError& err, std::string_view source_name);

}  // namespace seum
