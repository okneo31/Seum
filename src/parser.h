#pragma once

#include "ast.h"
#include "token.h"

namespace seum {

Program parse(const std::vector<Token>& tokens);

}  // namespace seum
