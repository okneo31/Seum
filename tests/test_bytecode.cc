#include <catch2/catch_amalgamated.hpp>

#include "bytecode.h"

using namespace seum;
using bytecode::Opcode;

TEST_CASE("바이트코드: opcode_name 은 한글 표면 (잠긴 결정 #20)", "[bytecode]") {
    REQUIRE(bytecode::opcode_name(Opcode::CONST_INT)    == U"정수상수");
    REQUIRE(bytecode::opcode_name(Opcode::CONST_STR)    == U"문자열상수");
    REQUIRE(bytecode::opcode_name(Opcode::LOAD_GLOBAL)  == U"전역가져오기");
    REQUIRE(bytecode::opcode_name(Opcode::STORE_GLOBAL) == U"전역두기");
    REQUIRE(bytecode::opcode_name(Opcode::CALL)         == U"부르기");
    REQUIRE(bytecode::opcode_name(Opcode::DROP)         == U"버리기");
    REQUIRE(bytecode::opcode_name(Opcode::IMPORT)       == U"가져오기");
    REQUIRE(bytecode::opcode_name(Opcode::HALT)         == U"종료");
}

TEST_CASE("바이트코드: 빈 Chunk 풀어쓰기는 빈 문자열", "[bytecode]") {
    bytecode::Chunk c;
    REQUIRE(bytecode::disassemble(c).empty());
}

TEST_CASE("바이트코드: HALT 1개 만 있는 Chunk", "[bytecode]") {
    bytecode::Chunk c;
    c.bytes.push_back(static_cast<std::uint8_t>(Opcode::HALT));
    c.positions.push_back({});
    auto out = bytecode::disassemble(c);
    REQUIRE(out.find(U"0x0000") != std::u32string::npos);
    REQUIRE(out.find(U"종료")    != std::u32string::npos);
}

TEST_CASE("바이트코드: CONST_INT 풀어쓰기는 정수 값을 디코드", "[bytecode]") {
    bytecode::Chunk c;
    c.int_pool.push_back(42);
    c.bytes = {
        static_cast<std::uint8_t>(Opcode::CONST_INT),
        0x00, 0x00, 0x00, 0x00,
        static_cast<std::uint8_t>(Opcode::HALT),
    };
    c.positions.push_back({});
    c.positions.push_back({});
    auto out = bytecode::disassemble(c);
    REQUIRE(out.find(U"정수상수") != std::u32string::npos);
    REQUIRE(out.find(U"42")       != std::u32string::npos);
}

TEST_CASE("바이트코드: 이름 풀이 따옴표로 감싸짐", "[bytecode]") {
    bytecode::Chunk c;
    c.name_pool.push_back(U"보여주기");
    c.bytes = {
        static_cast<std::uint8_t>(Opcode::LOAD_GLOBAL),
        0x00, 0x00, 0x00, 0x00,
        static_cast<std::uint8_t>(Opcode::HALT),
    };
    c.positions.push_back({});
    c.positions.push_back({});
    auto out = bytecode::disassemble(c);
    REQUIRE(out.find(U"전역가져오기")   != std::u32string::npos);
    REQUIRE(out.find(U"\"보여주기\"")   != std::u32string::npos);
}

// === v0.3b: serialize/deserialize round-trip ===

#include "ir.h"
#include "lexer.h"
#include "parser.h"

TEST_CASE("바이트코드 v0.3b: 빈 Program round-trip", "[bytecode][v03b]") {
    bytecode::Program prog;
    auto bytes = bytecode::serialize(prog);
    auto restored = bytecode::deserialize(bytes);
    REQUIRE(restored.main.chunk.bytes.empty());
    REQUIRE(restored.functions.empty());
}

TEST_CASE("바이트코드 v0.3b: 실제 소스 컴파일 round-trip", "[bytecode][v03b]") {
    auto ast = parse(tokenize(U"가져오기(시간). 변수 a = 1 + 2. 보여주기(a)."));
    auto ir_prog = ir::lower(ast);
    auto orig    = bytecode::compile(ir_prog);

    auto bytes    = bytecode::serialize(orig);
    auto restored = bytecode::deserialize(bytes);

    REQUIRE(restored.main.chunk.bytes      == orig.main.chunk.bytes);
    REQUIRE(restored.main.chunk.int_pool   == orig.main.chunk.int_pool);
    REQUIRE(restored.main.chunk.str_pool   == orig.main.chunk.str_pool);
    REQUIRE(restored.main.chunk.name_pool  == orig.main.chunk.name_pool);
    REQUIRE(restored.functions.size()      == orig.functions.size());
}

TEST_CASE("바이트코드 v0.3b: 함수 포함 Program round-trip", "[bytecode][v03b]") {
    auto ast = parse(tokenize(
        U"함수 두배(x) -> 결과 { 돌려주기 x * 2. }\n"
        U"보여주기(두배(21))."));
    auto orig = bytecode::compile(ir::lower(ast));

    auto bytes    = bytecode::serialize(orig);
    auto restored = bytecode::deserialize(bytes);

    REQUIRE(restored.functions.size() == 1);
    REQUIRE(restored.functions[0].name == U"두배");
    REQUIRE(restored.functions[0].params.size() == 1);
    REQUIRE(restored.functions[0].params[0] == U"x");
    REQUIRE(restored.functions[0].chunk.bytes == orig.functions[0].chunk.bytes);
}

TEST_CASE("바이트코드 v0.3b: bytes 직렬화 비트 동일성 (재직렬화 = 같은 바이트)", "[bytecode][v03b]") {
    auto orig = bytecode::compile(ir::lower(parse(tokenize(
        U"변수 인사 = \"세움\". 보여주기(인사)."))));
    auto b1 = bytecode::serialize(orig);
    auto restored = bytecode::deserialize(b1);
    auto b2 = bytecode::serialize(restored);
    REQUIRE(b1 == b2);
}
