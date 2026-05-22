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
