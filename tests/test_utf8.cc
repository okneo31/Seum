#include <catch2/catch_amalgamated.hpp>

#include "utf8.h"

using namespace seum;

TEST_CASE("UTF-8: 라틴 ASCII 라운드트립", "[utf8]") {
    auto cp = utf8::decode("hello");
    REQUIRE(cp == U"hello");
    REQUIRE(utf8::encode(cp) == "hello");
}

TEST_CASE("UTF-8: 한글 음절 디코드", "[utf8]") {
    auto cp = utf8::decode("\xEC\x95\x88\xEB\x85\x95");  // 안녕
    REQUIRE(cp == U"안녕");
}

TEST_CASE("UTF-8: 한글 음절 라운드트립", "[utf8]") {
    std::u32string s = U"세움 만세";
    REQUIRE(utf8::decode(utf8::encode(s)) == s);
}

TEST_CASE("UTF-8: overlong 거부", "[utf8]") {
    // U+0000을 2바이트로 부정 인코딩한 시퀀스
    REQUIRE_THROWS(utf8::decode(std::string("\xC0\x80", 2)));
}

TEST_CASE("UTF-8: 서로게이트 거부", "[utf8]") {
    // U+D800은 UTF-8 인코딩이 ED A0 80
    REQUIRE_THROWS(utf8::decode(std::string("\xED\xA0\x80", 3)));
}

TEST_CASE("UTF-8: 잘린 시퀀스 거부", "[utf8]") {
    REQUIRE_THROWS(utf8::decode(std::string("\xE0", 1)));
}

TEST_CASE("식별자 문자 판별", "[utf8]") {
    REQUIRE(utf8::is_identifier_start(U'a'));
    REQUIRE(utf8::is_identifier_start(U'_'));
    REQUIRE(utf8::is_identifier_start(U'안'));
    REQUIRE_FALSE(utf8::is_identifier_start(U'1'));
    REQUIRE(utf8::is_identifier_char(U'1'));
    REQUIRE(utf8::is_identifier_char(U'녕'));
    REQUIRE_FALSE(utf8::is_identifier_char(U' '));
}
