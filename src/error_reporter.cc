#include "error_reporter.h"

#include "utf8.h"

#include <iostream>
#include <sstream>

namespace seum {

void raise(Position pos, const std::string& message_utf8) {
    throw SeumError(message_utf8, pos);
}

void raise(Position pos, const std::u32string& message) {
    throw SeumError(utf8::encode(message), pos);
}

void report(const SeumError& err, std::string_view source_name) {
    std::ostringstream oss;
    oss << "오류: " << err.what()
        << "  (" << source_name
        << " " << err.position().line << ":" << err.position().column << ")";
    std::cerr << oss.str() << std::endl;
}

}  // namespace seum
