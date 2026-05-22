#include <catch2/catch_amalgamated.hpp>

#include "lexer.h"
#include "parser.h"

using namespace seum;

static Program parse_source(std::u32string_view src) {
    return parse(tokenize(src));
}

TEST_CASE("파서: 가져오기 문장", "[parser]") {
    Program p = parse_source(U"가져오기(시간).");
    REQUIRE(p.statements.size() == 1);
    REQUIRE(is_import_stmt(p.statements[0]));
    auto* imp = as_import_stmt(p.statements[0]);
    REQUIRE(imp != nullptr);
    REQUIRE(imp->module_name == U"시간");
}

TEST_CASE("파서: 변수 선언", "[parser]") {
    Program p = parse_source(U"변수 안녕 = \"세움\".");
    REQUIRE(is_var_decl_stmt(p.statements[0]));
    auto* v = as_var_decl_stmt(p.statements[0]);
    REQUIRE(v != nullptr);
    REQUIRE(v->name == U"안녕");
    REQUIRE(is_string_lit(v->value));
    auto* lit = as_string_lit(v->value);
    REQUIRE(lit != nullptr);
    REQUIRE(lit->value == U"세움");
}

TEST_CASE("파서: 함수 호출", "[parser]") {
    Program p = parse_source(U"보여주기(\"안녕\").");
    REQUIRE(is_expr_stmt(p.statements[0]));
    auto* es = as_expr_stmt(p.statements[0]);
    REQUIRE(es != nullptr);
    REQUIRE(is_call(es->expr));
    auto* call = as_call(es->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->args.size() == 1);
}

TEST_CASE("파서: 종결자 누락은 오류", "[parser]") {
    REQUIRE_THROWS(parse_source(U"보여주기(\"안녕\")"));
}

// === v0.4a-1: 레코드 리터럴 + 멤버 접근 (결정 69·79·92) ===

TEST_CASE("파서 v0.4a-1: 레코드 리터럴", "[parser][v04a]") {
    Program p = parse_source(U"변수 위치 = (ㄱ: 10, ㅅ: 20).");
    auto* v = as_var_decl_stmt(p.statements[0]);
    REQUIRE(v != nullptr);
    REQUIRE(is_record_lit(v->value));
    auto* rec = as_record_lit(v->value);
    REQUIRE(rec != nullptr);
    REQUIRE(rec->fields.size() == 2);
    REQUIRE(rec->fields[0].key == U"ㄱ");
    REQUIRE(rec->fields[1].key == U"ㅅ");
}

TEST_CASE("파서 v0.4a-1: 멤버 접근", "[parser][v04a]") {
    Program p = parse_source(U"보여주기(위치.ㄱ).");
    auto* es = as_expr_stmt(p.statements[0]);
    REQUIRE(es != nullptr);
    auto* call = as_call(es->expr);
    REQUIRE(call != nullptr);
    REQUIRE(call->args.size() == 1);
    REQUIRE(is_member(call->args[0]));
    auto* m = as_member(call->args[0]);
    REQUIRE(m != nullptr);
    REQUIRE(m->field == U"ㄱ");
    REQUIRE(is_identifier(m->target));
}

TEST_CASE("파서 v0.4a-1: 멤버 접근 체이닝", "[parser][v04a]") {
    Program p = parse_source(U"보여주기(용사.위치.ㄱ).");
    auto* es = as_expr_stmt(p.statements[0]);
    auto* call = as_call(es->expr);
    REQUIRE(call != nullptr);
    auto* outer = as_member(call->args[0]);   // (용사.위치).ㄱ
    REQUIRE(outer != nullptr);
    REQUIRE(outer->field == U"ㄱ");
    REQUIRE(is_member(outer->target));        // 용사.위치
    auto* inner = as_member(outer->target);
    REQUIRE(inner != nullptr);
    REQUIRE(inner->field == U"위치");
}

TEST_CASE("파서 v0.4a-1: 괄호식은 레코드가 아님", "[parser][v04a]") {
    // (1 + 2) * 3 — '(' 다음이 정수라 레코드 아님 (결정 92, 3-5)
    Program p = parse_source(U"보여주기((1 + 2) * 3).");
    auto* es = as_expr_stmt(p.statements[0]);
    auto* call = as_call(es->expr);
    REQUIRE(call != nullptr);
    REQUIRE(is_binary(call->args[0]));   // 곱셈식
}

TEST_CASE("파서 v0.4a-1: 레코드 중복 키는 오류 (점검 정리)", "[parser][v04a]") {
    REQUIRE_THROWS(parse_source(U"변수 r = (ㄱ: 1, ㄱ: 2)."));
}

// === v0.4a-2: 필드 대입 + 복합대입 (결정 81) ===

TEST_CASE("파서 v0.4a-2: 필드 대입 = / +=", "[parser][v04a2]") {
    Program p1 = parse_source(U"용사.위치.ㄱ = 0.");
    REQUIRE(is_assign_stmt(p1.statements[0]));
    auto* a1 = as_assign_stmt(p1.statements[0]);
    REQUIRE(a1 != nullptr);
    REQUIRE(a1->op == U"=");
    REQUIRE(is_member(a1->target));

    Program p2 = parse_source(U"용사.위치.ㄱ += 10.");
    auto* a2 = as_assign_stmt(p2.statements[0]);
    REQUIRE(a2 != nullptr);
    REQUIRE(a2->op == U"+=");
    REQUIRE(is_member(a2->target));
}

TEST_CASE("파서 v0.4a-2: 변수 재대입은 오류 (필드만 허용)", "[parser][v04a2]") {
    REQUIRE_THROWS(parse_source(U"x = 5."));
}

// === v0.4a-4: 계약 선언형 블록 (결정 63·87·90) ===

TEST_CASE("파서 v0.4a-4: 계약 블록 → 함수+레코드 desugar", "[parser][v04a4]") {
    // 계약 = 메서드(전역 함수) + 변수(레코드) 로 desugar
    Program p = parse_source(
        U"계약 은행 { 주인: \"철수\" 함수 인사() -> 결과 { 돌려주기 \"환영\". } }");
    REQUIRE(p.statements.size() == 2);
    REQUIRE(is_func_decl(p.statements[0]));
    REQUIRE(as_func_decl(p.statements[0])->name == U"인사");
    REQUIRE(is_var_decl_stmt(p.statements[1]));
    auto* vd = as_var_decl_stmt(p.statements[1]);
    REQUIRE(vd->name == U"은행");
    REQUIRE(is_record_lit(vd->value));
}

TEST_CASE("파서 v0.4a-4: 계약 중복 항목은 오류", "[parser][v04a4]") {
    REQUIRE_THROWS(parse_source(U"계약 X { 가: 1 가: 2 }"));
}

// === v0.4a-5b: 계약 자산 + 받아서 (결정 64·65) ===

TEST_CASE("파서 v0.4a-5b: 계약 자산 선언 → 통화(0) 필드로 desugar", "[parser][v04a5b]") {
    // 자산 금고: BTC → 레코드 필드 금고: BTC(0)
    Program p = parse_source(U"계약 X { 자산 금고: BTC }");
    REQUIRE(p.statements.size() == 1);
    REQUIRE(is_var_decl_stmt(p.statements[0]));
    auto* vd = as_var_decl_stmt(p.statements[0]);
    REQUIRE(is_record_lit(vd->value));
    auto* rec = as_record_lit(vd->value);
    REQUIRE(rec->fields.size() == 1);
    REQUIRE(rec->fields[0].key == U"금고");
    REQUIRE(is_call(rec->fields[0].value));   // BTC(0)
}

TEST_CASE("파서 v0.4a-5b: 받아서는 계약 밖에서 오류", "[parser][v04a5b]") {
    REQUIRE_THROWS(parse_source(U"함수 예금(돈) 받아서 -> 금고 { }"));
}

// === v0.4a-5c: 통화 발행 + -= (결정 64) ===

TEST_CASE("파서 v0.4a-5c: 통화 선언 → 통화등록 호출로 desugar", "[parser][v04a5c]") {
    Program p = parse_source(U"통화 마실.");
    REQUIRE(p.statements.size() == 1);
    REQUIRE(is_var_decl_stmt(p.statements[0]));
    auto* v = as_var_decl_stmt(p.statements[0]);
    REQUIRE(v->name == U"마실");
    REQUIRE(is_call(v->value));
}

TEST_CASE("파서 v0.4a-5c: 통화 선언 페그식 허용", "[parser][v04a5c]") {
    REQUIRE_NOTHROW(parse_source(U"통화 마실 = BTC / 100000."));
}
