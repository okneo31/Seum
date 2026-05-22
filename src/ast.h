#pragma once

#include "common.h"

namespace seum {

// Forward declarations for recursive types stored in std::variant via unique_ptr.
struct IntLitExpr;
struct StringLitExpr;
struct BoolLitExpr;
struct IdentifierExpr;
struct CallExpr;
struct BinaryExpr;
struct UnaryExpr;
struct UnsafeExpr;            // v0.3e: 위험 { ... } 식
struct RecordExpr;            // v0.4a-1: 레코드 리터럴 (#69)
struct MemberExpr;            // v0.4a-1: 멤버 접근 (#79)
struct IfStmt;
struct RepeatStmt;
struct WhileStmt;
struct FuncDeclStmt;
struct ReturnStmt;

using Expr = std::variant<
    std::unique_ptr<IntLitExpr>,
    std::unique_ptr<StringLitExpr>,
    std::unique_ptr<BoolLitExpr>,
    std::unique_ptr<IdentifierExpr>,
    std::unique_ptr<CallExpr>,
    std::unique_ptr<BinaryExpr>,
    std::unique_ptr<UnaryExpr>,
    std::unique_ptr<UnsafeExpr>,
    std::unique_ptr<RecordExpr>,
    std::unique_ptr<MemberExpr>
>;

struct IntLitExpr {
    std::int64_t value;
    Position pos;
};

struct StringLitExpr {
    std::u32string value;
    Position pos;
};

struct BoolLitExpr {
    bool value;
    Position pos;
};

struct IdentifierExpr {
    std::u32string name;
    Position pos;
};

struct CallExpr {
    Expr callee;
    std::vector<Expr> args;
    Position pos;
};

// op 는 토큰 그대로의 글리프 (`==`, `&&`, `<=` 등). 인터프리터·IR 모두 op 로 분기.
struct BinaryExpr {
    std::u32string op;
    Expr lhs;
    Expr rhs;
    Position pos;
};

struct UnaryExpr {
    std::u32string op;  // `!`, 향후 `-`
    Expr operand;
    Position pos;
};

// 위험 { 식 } — 결정 #58. v0.3e: 식 형태 (블록 형태 = v0.5).
// inner 는 보통 native_xxx() 호출. v0.3e 는 단순 wrap (의미 marker).
// v0.5+ 에서 sandboxing/권한 검사 본격 도입.
struct UnsafeExpr {
    Expr      inner;
    Position  pos;
};

// 레코드 리터럴 (키: 값, …) — 결정 #69. 범용 자모-키 레코드.
struct RecordField {
    std::u32string key;
    Expr           value;
};
struct RecordExpr {
    std::vector<RecordField> fields;
    Position pos;
};

// 멤버 접근 `대상.필드` — 결정 #79. lexer 가 Dot 토큰으로 구분.
struct MemberExpr {
    Expr           target;
    std::u32string field;
    Position pos;
};

// === Statements ===
struct ImportStmt {
    std::u32string module_name;
    Position pos;
};

struct VarDeclStmt {
    std::u32string name;
    Expr value;
    Position pos;
};

struct ExprStmt {
    Expr expr;
    Position pos;
};

// 필드 대입 `대상.필드 = 식.` / `대상.필드 += 식.` — 결정 #81.
// target 은 MemberExpr (레코드·계약 필드). op = U"=" 또는 U"+=".
struct AssignStmt {
    Expr           target;
    std::u32string op;
    Expr           value;
    Position       pos;
};

// IfStmt/Repeat/While 는 vector<Stmt> 를 들기 때문에 Stmt variant 에서 unique_ptr 로 보관.
using Stmt = std::variant<
    ImportStmt,
    VarDeclStmt,
    ExprStmt,
    AssignStmt,
    std::unique_ptr<IfStmt>,
    std::unique_ptr<RepeatStmt>,
    std::unique_ptr<WhileStmt>,
    std::unique_ptr<FuncDeclStmt>,
    std::unique_ptr<ReturnStmt>
>;

// 만약 (조건) 이면 { … } 아니면 { … } — 결정 #28.
struct IfStmt {
    Expr cond;
    std::vector<Stmt> then_body;
    std::vector<Stmt> else_body;
    Position pos;
};

// 반복 N번 { 본문 } — 결정 #29. N <= 0 이면 0회.
struct RepeatStmt {
    Expr count;
    std::vector<Stmt> body;
    Position pos;
};

// 동안 (조건) { 본문 } — 결정 #29. cond 가 bool 이어야 함.
struct WhileStmt {
    Expr cond;
    std::vector<Stmt> body;
    Position pos;
};

// 함수 이름(p1, p2) -> 결과 { … } — 결정 #30.
// 또는 지연값 이름() -> 결과 { … } — 결정 #59 (is_getter=true, params empty).
struct FuncDeclStmt {
    std::u32string             name;
    std::vector<std::u32string> params;
    std::u32string             result_name;
    std::vector<Stmt>          body;
    bool                       is_getter{false};   // v0.3e #59 — `지연값` syntax
    Position                   pos;
};

// 돌려주기 [식]. value 가 없으면 monostate (없음) 반환.
struct ReturnStmt {
    Expr      value;
    bool      has_value;     // false 이면 value 무시
    Position  pos;
};

struct Program {
    std::vector<Stmt> statements;
};

Position position_of(const Expr& e);

// === 변형 질의 헬퍼 ===
bool is_int_lit   (const Expr& e);
bool is_string_lit(const Expr& e);
bool is_bool_lit  (const Expr& e);
bool is_identifier(const Expr& e);
bool is_call      (const Expr& e);
bool is_binary    (const Expr& e);
bool is_unary     (const Expr& e);
bool is_unsafe    (const Expr& e);
bool is_record_lit(const Expr& e);
bool is_member    (const Expr& e);

const IntLitExpr*     as_int_lit   (const Expr& e);
const StringLitExpr*  as_string_lit(const Expr& e);
const BoolLitExpr*    as_bool_lit  (const Expr& e);
const IdentifierExpr* as_identifier(const Expr& e);
const CallExpr*       as_call      (const Expr& e);
const BinaryExpr*     as_binary    (const Expr& e);
const UnaryExpr*      as_unary     (const Expr& e);
const UnsafeExpr*     as_unsafe    (const Expr& e);
const RecordExpr*     as_record_lit(const Expr& e);
const MemberExpr*     as_member    (const Expr& e);

bool is_import_stmt   (const Stmt& s);
bool is_var_decl_stmt (const Stmt& s);
bool is_expr_stmt     (const Stmt& s);
bool is_assign_stmt   (const Stmt& s);
bool is_if_stmt       (const Stmt& s);
bool is_repeat_stmt   (const Stmt& s);
bool is_while_stmt    (const Stmt& s);
bool is_func_decl     (const Stmt& s);
bool is_return_stmt   (const Stmt& s);

const ImportStmt*    as_import_stmt   (const Stmt& s);
const VarDeclStmt*   as_var_decl_stmt (const Stmt& s);
const ExprStmt*      as_expr_stmt     (const Stmt& s);
const AssignStmt*    as_assign_stmt   (const Stmt& s);
const IfStmt*        as_if_stmt       (const Stmt& s);
const RepeatStmt*    as_repeat_stmt   (const Stmt& s);
const WhileStmt*     as_while_stmt    (const Stmt& s);
const FuncDeclStmt*  as_func_decl     (const Stmt& s);
const ReturnStmt*    as_return_stmt   (const Stmt& s);

}  // namespace seum
