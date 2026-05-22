#include "token.h"

namespace seum {

const char* token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::Identifier: return "식별자";
        case TokenKind::Keyword:    return "키워드";
        case TokenKind::Integer:    return "정수";
        case TokenKind::String:     return "문자열";
        case TokenKind::LParen:     return "(";
        case TokenKind::RParen:     return ")";
        case TokenKind::LBrace:     return "{";
        case TokenKind::RBrace:     return "}";
        case TokenKind::Comma:      return ",";
        case TokenKind::Colon:      return ":";
        case TokenKind::Equals:     return "=";
        case TokenKind::Period:     return ".";
        case TokenKind::EqEq:       return "==";
        case TokenKind::BangEq:     return "!=";
        case TokenKind::Lt:         return "<";
        case TokenKind::Gt:         return ">";
        case TokenKind::LtEq:       return "<=";
        case TokenKind::GtEq:       return ">=";
        case TokenKind::Bang:       return "!";
        case TokenKind::AmpAmp:     return "&&";
        case TokenKind::PipePipe:   return "||";
        case TokenKind::Plus:       return "+";
        case TokenKind::Minus:      return "-";
        case TokenKind::Star:       return "*";
        case TokenKind::Slash:      return "/";
        case TokenKind::Percent:    return "%";
        case TokenKind::Arrow:      return "->";
        case TokenKind::EndOfFile:  return "파일끝";
    }
    return "?";
}

}  // namespace seum
