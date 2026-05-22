#pragma once

#include "common.h"

namespace seum {

enum class TokenKind {
    Identifier,
    Keyword,
    Integer,
    String,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Equals,       // =
    Period,
    Dot,          // v0.4a-1 (#79) — 멤버 접근 '.' (양쪽 공백 없이 인접)
    // v0.2b 추가 (잠긴 결정 #26).
    EqEq,         // ==
    BangEq,       // !=
    Lt,           // <
    Gt,           // >
    LtEq,         // <=
    GtEq,         // >=
    Bang,         // !
    AmpAmp,       // &&
    PipePipe,     // ||
    // v0.2c 산술 (#26).
    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /
    Percent,      // %
    // v0.2e (#30).
    Arrow,        // ->
    PlusEq,       // += (v0.4a-2 #81) — 복합대입
    EndOfFile,
};

struct Token {
    TokenKind kind;
    std::u32string text;
    Position pos;
};

const char* token_kind_name(TokenKind k);

}  // namespace seum
