#include <catch2/catch_amalgamated.hpp>

#include "lexer.h"

using namespace seum;

TEST_CASE("렉서: 한글 식별자", "[lexer]") {
    auto toks = tokenize(U"보여주기");
    REQUIRE(toks.size() == 2);
    REQUIRE(toks[0].kind == TokenKind::Identifier);
    REQUIRE(toks[0].text == U"보여주기");
    REQUIRE(toks[1].kind == TokenKind::EndOfFile);
}

TEST_CASE("렉서: 키워드 인식", "[lexer]") {
    auto toks = tokenize(U"가져오기");
    REQUIRE(toks[0].kind == TokenKind::Keyword);
    REQUIRE(toks[0].text == U"가져오기");
}

TEST_CASE("렉서: 정수 리터럴", "[lexer]") {
    auto toks = tokenize(U"42");
    REQUIRE(toks[0].kind == TokenKind::Integer);
    REQUIRE(toks[0].text == U"42");
}

TEST_CASE("렉서: 문자열 리터럴", "[lexer]") {
    auto toks = tokenize(U"\"세움\"");
    REQUIRE(toks[0].kind == TokenKind::String);
    REQUIRE(toks[0].text == U"세움");
}

TEST_CASE("렉서: 한 줄 종합", "[lexer]") {
    auto toks = tokenize(U"보여주기(\"세움\").");
    REQUIRE(toks.size() == 6);
    REQUIRE(toks[0].kind == TokenKind::Identifier);   // 보여주기
    REQUIRE(toks[1].kind == TokenKind::LParen);
    REQUIRE(toks[2].kind == TokenKind::String);       // 세움
    REQUIRE(toks[3].kind == TokenKind::RParen);
    REQUIRE(toks[4].kind == TokenKind::Period);
    REQUIRE(toks[5].kind == TokenKind::EndOfFile);
}

TEST_CASE("렉서: 줄 주석", "[lexer]") {
    auto toks = tokenize(U"// 무시되는 주석\n보여주기");
    REQUIRE(toks[0].kind == TokenKind::Identifier);
    REQUIRE(toks[0].text == U"보여주기");
}

TEST_CASE("렉서: 위치 정보", "[lexer]") {
    auto toks = tokenize(U"안녕\n세상");
    REQUIRE(toks[0].pos.line == 1);
    REQUIRE(toks[1].pos.line == 2);
}

// v0.4a-1 (결정 79): 멤버 접근 '.' 은 양쪽 공백 없이 인접할 때만 Dot,
// 그 외(뒤가 식별자-시작이 아니거나 앞에 공백)는 문장 종결자 Period.
TEST_CASE("렉서: 멤버 접근 '.' vs 종결자 '.' — 결정 79", "[lexer]") {
    // 멤버 접근: 양쪽 공백 없이 인접 → Dot
    auto m = tokenize(U"위치.ㄱ");
    REQUIRE(m.size() == 4);
    REQUIRE(m[0].kind == TokenKind::Identifier);
    REQUIRE(m[1].kind == TokenKind::Dot);
    REQUIRE(m[2].kind == TokenKind::Identifier);
    REQUIRE(m[2].text == U"ㄱ");

    // 체이닝: 용사.위치.ㄱ → Dot 두 개
    auto c = tokenize(U"용사.위치.ㄱ");
    REQUIRE(c.size() == 6);
    REQUIRE(c[1].kind == TokenKind::Dot);
    REQUIRE(c[3].kind == TokenKind::Dot);

    // 문장 종결자: 뒤가 EOF/개행 → Period
    auto t = tokenize(U"안녕.");
    REQUIRE(t[1].kind == TokenKind::Period);

    // 앞에 공백이 인접을 깨면 종결자
    auto s = tokenize(U"위치 .ㄱ");
    REQUIRE(s[1].kind == TokenKind::Period);

    // 정수 사이 '.' 은 종결자 (소수점 아님 — 정수만 존재, 회귀 가드)
    auto n = tokenize(U"3.14");
    REQUIRE(n[1].kind == TokenKind::Period);
}
