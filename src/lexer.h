#pragma once

#include "common.h"
#include "token.h"

namespace seum {

// 입력은 이미 UTF-8 → UTF-32 디코드된 코드포인트 시퀀스.
// 출력은 토큰 벡터. 마지막은 항상 EndOfFile.
// 오류는 SeumError로 던진다.
std::vector<Token> tokenize(std::u32string_view source);

}  // namespace seum
