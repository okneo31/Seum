#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace seum {

struct Position {
    std::size_t line{1};
    std::size_t column{1};
};

}  // namespace seum
