#include "lexer.h"

#include "error_reporter.h"
#include "utf8.h"

#include <unordered_set>

namespace seum {

namespace {

const std::unordered_set<std::u32string>& keywords() {
    static const std::unordered_set<std::u32string> set{
        U"가져오기",
        U"변수",
        U"함수",
        U"돌려주기",   // v0.2e (#30).
        U"지연값",     // v0.3e (#59) — Getter 1급 syntax
        U"계약",
        U"자산",
        U"위험",
        U"받아서",
        U"누르면",
        // v0.2b (잠긴 결정 #27, #28).
        U"참",
        U"거짓",
        U"만약",
        U"이면",
        U"아니면",
        // v0.2d (잠긴 결정 #29).
        U"반복",
        U"번",
        U"동안",
        U"없음",
    };
    return set;
}

class Lexer {
public:
    explicit Lexer(std::u32string_view src) : src_(src) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        while (true) {
            skip_trivia();
            if (i_ >= src_.size()) break;
            Token t = next_token();
            out.push_back(std::move(t));
        }
        out.push_back({TokenKind::EndOfFile, U"", here()});
        return out;
    }

private:
    char32_t peek(std::size_t k = 0) const {
        if (i_ + k >= src_.size()) return 0;
        return src_[i_ + k];
    }

    char32_t advance() {
        char32_t c = src_[i_++];
        if (c == U'\n') {
            line_++;
            col_ = 1;
        } else {
            col_++;
        }
        return c;
    }

    Position here() const { return {line_, col_}; }

    void skip_trivia() {
        while (i_ < src_.size()) {
            char32_t c = peek();
            if (utf8::is_whitespace(c)) {
                advance();
            } else if (c == U'/' && peek(1) == U'/') {
                // 줄 주석
                while (i_ < src_.size() && peek() != U'\n') advance();
            } else if (c == U'/' && peek(1) == U'*') {
                advance(); advance();
                while (i_ < src_.size() && !(peek() == U'*' && peek(1) == U'/')) {
                    advance();
                }
                if (i_ >= src_.size()) {
                    raise(here(), "닫히지 않은 주석");
                }
                advance(); advance();
            } else {
                break;
            }
        }
    }

    Token next_token() {
        Position start = here();
        char32_t c = peek();

        if (utf8::is_digit(c)) return read_number(start);
        if (c == U'"') return read_string(start);
        if (utf8::is_identifier_start(c)) return read_identifier(start);

        // 2글자 기호 우선 처리 (v0.2b).
        if (c == U'=' && peek(1) == U'=') { advance(); advance(); return {TokenKind::EqEq,     U"==", start}; }
        if (c == U'!' && peek(1) == U'=') { advance(); advance(); return {TokenKind::BangEq,   U"!=", start}; }
        if (c == U'<' && peek(1) == U'=') { advance(); advance(); return {TokenKind::LtEq,     U"<=", start}; }
        if (c == U'>' && peek(1) == U'=') { advance(); advance(); return {TokenKind::GtEq,     U">=", start}; }
        if (c == U'&' && peek(1) == U'&') { advance(); advance(); return {TokenKind::AmpAmp,   U"&&", start}; }
        if (c == U'|' && peek(1) == U'|') { advance(); advance(); return {TokenKind::PipePipe, U"||", start}; }
        if (c == U'-' && peek(1) == U'>') { advance(); advance(); return {TokenKind::Arrow,    U"->", start}; }

        // 1글자 기호
        advance();
        switch (c) {
            case U'(': return {TokenKind::LParen,  U"(", start};
            case U')': return {TokenKind::RParen,  U")", start};
            case U'{': return {TokenKind::LBrace,  U"{", start};
            case U'}': return {TokenKind::RBrace,  U"}", start};
            case U',': return {TokenKind::Comma,   U",", start};
            case U':': return {TokenKind::Colon,   U":", start};
            case U'=': return {TokenKind::Equals,  U"=", start};
            case U'.': return {TokenKind::Period,  U".", start};
            case U'<': return {TokenKind::Lt,      U"<", start};
            case U'>': return {TokenKind::Gt,      U">", start};
            case U'!': return {TokenKind::Bang,    U"!", start};
            // v0.2c 산술.
            case U'+': return {TokenKind::Plus,    U"+", start};
            case U'-': return {TokenKind::Minus,   U"-", start};
            case U'*': return {TokenKind::Star,    U"*", start};
            case U'/': return {TokenKind::Slash,   U"/", start};
            case U'%': return {TokenKind::Percent, U"%", start};
        }
        std::u32string msg = U"알 수 없는 문자: '";
        msg.push_back(c);
        msg += U"'";
        raise(start, msg);
    }

    Token read_number(Position start) {
        std::u32string buf;
        while (i_ < src_.size() && utf8::is_digit(peek())) {
            buf.push_back(advance());
        }
        return {TokenKind::Integer, std::move(buf), start};
    }

    Token read_string(Position start) {
        advance();  // opening "
        std::u32string buf;
        while (i_ < src_.size() && peek() != U'"') {
            char32_t c = advance();
            if (c == U'\\') {
                if (i_ >= src_.size()) raise(start, "닫히지 않은 문자열 (\\ 뒤에 입력 없음)");
                char32_t esc = advance();
                switch (esc) {
                    case U'n':  buf.push_back(U'\n'); break;
                    case U't':  buf.push_back(U'\t'); break;
                    case U'r':  buf.push_back(U'\r'); break;
                    case U'\\': buf.push_back(U'\\'); break;
                    case U'"':  buf.push_back(U'"');  break;
                    default:
                        buf.push_back(U'\\');
                        buf.push_back(esc);
                }
            } else {
                buf.push_back(c);
            }
        }
        if (i_ >= src_.size()) {
            raise(start, "닫히지 않은 문자열");
        }
        advance();  // closing "
        return {TokenKind::String, std::move(buf), start};
    }

    Token read_identifier(Position start) {
        std::u32string buf;
        while (i_ < src_.size() && utf8::is_identifier_char(peek())) {
            buf.push_back(advance());
        }
        TokenKind kind = keywords().count(buf) ? TokenKind::Keyword : TokenKind::Identifier;
        return {kind, std::move(buf), start};
    }

    std::u32string_view src_;
    std::size_t i_{0};
    std::size_t line_{1};
    std::size_t col_{1};
};

}  // namespace

std::vector<Token> tokenize(std::u32string_view source) {
    Lexer l(source);
    return l.run();
}

}  // namespace seum
