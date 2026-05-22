#pragma once

#include "ast.h"
#include "environment.h"

namespace seum {

// 트리워킹 인터프리터 v0. 프로그램을 평가하며 stdout 부수효과를 일으킨다.
void evaluate(const Program& program, Environment& env);

// 단일 식 평가 (테스트 용도).
Value evaluate_expr(const Expr& e, Environment& env);

}  // namespace seum
