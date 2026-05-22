#include <catch2/catch_amalgamated.hpp>

#include "jamo_huffman.h"

using namespace seum::jamo;

TEST_CASE("자모: decompose 단일 음절", "[jamo]") {
    REQUIRE(decompose(U"가")   == U"ㄱㅏ");
    REQUIRE(decompose(U"한")   == U"ㅎㅏㄴ");
    REQUIRE(decompose(U"있")   == U"ㅇㅣㅆ");
}

TEST_CASE("자모: decompose 여러 음절", "[jamo]") {
    REQUIRE(decompose(U"세움") == U"ㅅㅔㅇㅜㅁ");
    REQUIRE(decompose(U"담음") == U"ㄷㅏㅁㅇㅡㅁ");
}

TEST_CASE("자모: decompose 비-한글 보존", "[jamo]") {
    REQUIRE(decompose(U"abc")  == U"abc");
    REQUIRE(decompose(U"세움 1.0") == U"ㅅㅔㅇㅜㅁ 1.0");
}

TEST_CASE("자모: compose round-trip", "[jamo]") {
    auto check = [](const std::u32string& orig) {
        REQUIRE(compose(decompose(orig)) == orig);
    };
    check(U"가");
    check(U"세움세상의 평화를 위한 그릇");
    check(U"한국어 hello 1.0");
    check(U"안녕하세요 반갑습니다");
    check(U"");
    check(U"각인");   // 종성·다음초성 ambiguity 테스트
    check(U"가긴");   // 종성 없는 케이스
    check(U"한글");
}

TEST_CASE("자모: 압축 round-trip 비트 동일", "[jamo]") {
    auto roundtrip = [](const std::u32string& orig) {
        auto c = compress(orig);
        auto back = decompress(c);
        REQUIRE(back == orig);
    };
    roundtrip(U"");
    roundtrip(U"가");
    roundtrip(U"세움");
    roundtrip(U"세움세상의 평화를 위한 그릇");
    roundtrip(U"한국어 hello 1.0 abc 가나다");
    roundtrip(U"반복 N번 { 보여주기(\"야!\"). }");
}

TEST_CASE("자모: 압축 결과가 빈도 정렬에 결정적 (재압축 = 같은 바이트)", "[jamo]") {
    auto text = U"안녕하세요 세움 세움 세움";
    auto a = compress(text);
    auto b = compress(text);
    REQUIRE(a == b);
}

TEST_CASE("자모: 단일 문자 압축 (degenerate 1-symbol Huffman)", "[jamo]") {
    // 모두 같은 char32 인 경우 — Huffman tree 가 leaf 만. 비트 0/1 무관.
    REQUIRE(decompress(compress(U"가가가가가")) == U"가가가가가");
    REQUIRE(decompress(compress(U"aaaaaa"))     == U"aaaaaa");
}

TEST_CASE("자모: 긴 텍스트는 압축률 < 1.0 (smoke)", "[jamo]") {
    std::u32string text;
    for (int i = 0; i < 100; ++i) {
        text += U"세움세상의 평화를 위한 그릇 한국어 한국어 한국어 ";
    }
    auto c = compress(text);
    // text 의 UTF-32 raw 크기 = chars × 4 bytes
    std::size_t raw_size = text.size() * 4;
    // 압축 결과가 raw UTF-32 보다 작아야 한다는 약한 검증
    REQUIRE(c.size() < raw_size);
}
