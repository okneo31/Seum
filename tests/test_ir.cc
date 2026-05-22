#include <catch2/catch_amalgamated.hpp>

#include "ir.h"
#include "lexer.h"
#include "parser.h"

using namespace seum;

static ir::Program lower_source(std::u32string_view src) {
    return ir::lower(parse(tokenize(src)));
}

TEST_CASE("IR: 빈 프로그램 → 빈 main", "[ir]") {
    ir::Program p = lower_source(U"");
    REQUIRE(p.main.instrs.empty());
    REQUIRE(p.functions.empty());
}

TEST_CASE("IR: 가져오기 문 → main IMPORT 1개", "[ir]") {
    ir::Program p = lower_source(U"가져오기(시간).");
    REQUIRE(p.main.instrs.size() == 1);
    REQUIRE(p.main.instrs[0].op   == ir::Op::IMPORT);
    REQUIRE(p.main.instrs[0].name == U"시간");
}

TEST_CASE("IR: 변수 선언 → CONST_STR + STORE_GLOBAL (main 은 글로벌)", "[ir]") {
    ir::Program p = lower_source(U"변수 안녕 = \"세움\".");
    REQUIRE(p.main.instrs.size() == 2);
    REQUIRE(p.main.instrs[0].op      == ir::Op::CONST_STR);
    REQUIRE(p.main.instrs[0].str_val == U"세움");
    REQUIRE(p.main.instrs[1].op      == ir::Op::STORE_GLOBAL);
    REQUIRE(p.main.instrs[1].name    == U"안녕");
}

TEST_CASE("IR: 함수 호출 → LOAD_GLOBAL + CONST_STR + CALL + DROP", "[ir]") {
    ir::Program p = lower_source(U"보여주기(\"안녕\").");
    REQUIRE(p.main.instrs.size() == 4);
    REQUIRE(p.main.instrs[0].op == ir::Op::LOAD_GLOBAL);
    REQUIRE(p.main.instrs[0].name == U"보여주기");
    REQUIRE(p.main.instrs[1].op == ir::Op::CONST_STR);
    REQUIRE(p.main.instrs[1].str_val == U"안녕");
    REQUIRE(p.main.instrs[2].op == ir::Op::CALL);
    REQUIRE(p.main.instrs[2].arg_srcs.size() == 1);
    REQUIRE(p.main.instrs[3].op == ir::Op::DROP);
}

TEST_CASE("IR: acceptance 1 sequence", "[ir]") {
    ir::Program p = lower_source(U"가져오기(시간).\n보여주기(지금).");
    REQUIRE(p.main.instrs.size() == 5);
    REQUIRE(p.main.instrs[0].op == ir::Op::IMPORT);
    REQUIRE(p.main.instrs[1].op == ir::Op::LOAD_GLOBAL);
    REQUIRE(p.main.instrs[1].name == U"보여주기");
    REQUIRE(p.main.instrs[2].op == ir::Op::LOAD_GLOBAL);
    REQUIRE(p.main.instrs[2].name == U"지금");
    REQUIRE(p.main.instrs[3].op == ir::Op::CALL);
    REQUIRE(p.main.instrs[4].op == ir::Op::DROP);
}

TEST_CASE("IR: 풀어쓰기에 한글 op 이름 포함", "[ir]") {
    ir::Program p = lower_source(U"가져오기(시간).");
    auto out = ir::disassemble(p);
    REQUIRE(out.find(U"가져오기") != std::u32string::npos);
}

// === v0.2e: 함수 정의 ===

TEST_CASE("IR v0.2e: 함수 정의는 별도 Function 으로", "[ir][v02e]") {
    ir::Program p = lower_source(U"함수 더하기(a, b) -> 합 { 돌려주기 a + b. }");
    REQUIRE(p.main.instrs.empty());
    REQUIRE(p.functions.size() == 1);
    REQUIRE(p.functions[0].name == U"더하기");
    REQUIRE(p.functions[0].params.size() == 2);
    REQUIRE(p.functions[0].params[0] == U"a");
    REQUIRE(p.functions[0].params[1] == U"b");
}

TEST_CASE("IR v0.2e: 매개변수 참조는 LOAD_LOCAL", "[ir][v02e]") {
    ir::Program p = lower_source(U"함수 둘배(x) -> 결과 { 돌려주기 x * 2. }");
    REQUIRE(p.functions.size() == 1);
    const auto& fn = p.functions[0];
    bool has_load_local = false;
    for (const auto& I : fn.instrs) {
        if (I.op == ir::Op::LOAD_LOCAL && I.local_idx == 0) {
            has_load_local = true; break;
        }
    }
    REQUIRE(has_load_local);
}

TEST_CASE("IR v0.2e: 함수 본문 끝에 implicit PUSH_NIL + RET", "[ir][v02e]") {
    ir::Program p = lower_source(U"함수 비어() -> 결과 { }");
    REQUIRE(p.functions.size() == 1);
    const auto& fn = p.functions[0];
    REQUIRE(fn.instrs.size() >= 2);
    REQUIRE(fn.instrs.back().op == ir::Op::RET);
    REQUIRE(fn.instrs[fn.instrs.size() - 2].op == ir::Op::PUSH_NIL);
}
