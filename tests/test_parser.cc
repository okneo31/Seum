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
