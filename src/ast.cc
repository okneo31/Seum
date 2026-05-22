#include "ast.h"

namespace seum {

Position position_of(const Expr& e) {
    if (auto p = std::get_if<std::unique_ptr<IntLitExpr>>(&e))    return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<StringLitExpr>>(&e)) return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<BoolLitExpr>>(&e))   return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<IdentifierExpr>>(&e)) return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<CallExpr>>(&e))      return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<BinaryExpr>>(&e))    return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<UnaryExpr>>(&e))     return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<UnsafeExpr>>(&e))    return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<RecordExpr>>(&e))    return (*p)->pos;
    if (auto p = std::get_if<std::unique_ptr<MemberExpr>>(&e))    return (*p)->pos;
    return {};
}

bool is_int_lit   (const Expr& e) { return std::holds_alternative<std::unique_ptr<IntLitExpr>>(e); }
bool is_string_lit(const Expr& e) { return std::holds_alternative<std::unique_ptr<StringLitExpr>>(e); }
bool is_bool_lit  (const Expr& e) { return std::holds_alternative<std::unique_ptr<BoolLitExpr>>(e); }
bool is_identifier(const Expr& e) { return std::holds_alternative<std::unique_ptr<IdentifierExpr>>(e); }
bool is_call      (const Expr& e) { return std::holds_alternative<std::unique_ptr<CallExpr>>(e); }
bool is_binary    (const Expr& e) { return std::holds_alternative<std::unique_ptr<BinaryExpr>>(e); }
bool is_unary     (const Expr& e) { return std::holds_alternative<std::unique_ptr<UnaryExpr>>(e); }
bool is_unsafe    (const Expr& e) { return std::holds_alternative<std::unique_ptr<UnsafeExpr>>(e); }
bool is_record_lit(const Expr& e) { return std::holds_alternative<std::unique_ptr<RecordExpr>>(e); }
bool is_member    (const Expr& e) { return std::holds_alternative<std::unique_ptr<MemberExpr>>(e); }

const IntLitExpr* as_int_lit(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<IntLitExpr>>(&e);
    return p ? p->get() : nullptr;
}
const StringLitExpr* as_string_lit(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<StringLitExpr>>(&e);
    return p ? p->get() : nullptr;
}
const BoolLitExpr* as_bool_lit(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<BoolLitExpr>>(&e);
    return p ? p->get() : nullptr;
}
const IdentifierExpr* as_identifier(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<IdentifierExpr>>(&e);
    return p ? p->get() : nullptr;
}
const CallExpr* as_call(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<CallExpr>>(&e);
    return p ? p->get() : nullptr;
}
const BinaryExpr* as_binary(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<BinaryExpr>>(&e);
    return p ? p->get() : nullptr;
}
const UnaryExpr* as_unary(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<UnaryExpr>>(&e);
    return p ? p->get() : nullptr;
}
const UnsafeExpr* as_unsafe(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<UnsafeExpr>>(&e);
    return p ? p->get() : nullptr;
}
const RecordExpr* as_record_lit(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<RecordExpr>>(&e);
    return p ? p->get() : nullptr;
}
const MemberExpr* as_member(const Expr& e) {
    auto p = std::get_if<std::unique_ptr<MemberExpr>>(&e);
    return p ? p->get() : nullptr;
}

bool is_import_stmt   (const Stmt& s) { return std::holds_alternative<ImportStmt>(s); }
bool is_var_decl_stmt (const Stmt& s) { return std::holds_alternative<VarDeclStmt>(s); }
bool is_expr_stmt     (const Stmt& s) { return std::holds_alternative<ExprStmt>(s); }
bool is_assign_stmt   (const Stmt& s) { return std::holds_alternative<AssignStmt>(s); }
bool is_if_stmt       (const Stmt& s) { return std::holds_alternative<std::unique_ptr<IfStmt>>(s); }
bool is_repeat_stmt   (const Stmt& s) { return std::holds_alternative<std::unique_ptr<RepeatStmt>>(s); }
bool is_while_stmt    (const Stmt& s) { return std::holds_alternative<std::unique_ptr<WhileStmt>>(s); }
bool is_func_decl     (const Stmt& s) { return std::holds_alternative<std::unique_ptr<FuncDeclStmt>>(s); }
bool is_return_stmt   (const Stmt& s) { return std::holds_alternative<std::unique_ptr<ReturnStmt>>(s); }

const ImportStmt*  as_import_stmt  (const Stmt& s) { return std::get_if<ImportStmt>(&s); }
const VarDeclStmt* as_var_decl_stmt(const Stmt& s) { return std::get_if<VarDeclStmt>(&s); }
const ExprStmt*    as_expr_stmt    (const Stmt& s) { return std::get_if<ExprStmt>(&s); }
const AssignStmt*  as_assign_stmt  (const Stmt& s) { return std::get_if<AssignStmt>(&s); }
const IfStmt*      as_if_stmt(const Stmt& s) {
    auto p = std::get_if<std::unique_ptr<IfStmt>>(&s);
    return p ? p->get() : nullptr;
}
const RepeatStmt*  as_repeat_stmt(const Stmt& s) {
    auto p = std::get_if<std::unique_ptr<RepeatStmt>>(&s);
    return p ? p->get() : nullptr;
}
const WhileStmt*   as_while_stmt(const Stmt& s) {
    auto p = std::get_if<std::unique_ptr<WhileStmt>>(&s);
    return p ? p->get() : nullptr;
}
const FuncDeclStmt* as_func_decl(const Stmt& s) {
    auto p = std::get_if<std::unique_ptr<FuncDeclStmt>>(&s);
    return p ? p->get() : nullptr;
}
const ReturnStmt*   as_return_stmt(const Stmt& s) {
    auto p = std::get_if<std::unique_ptr<ReturnStmt>>(&s);
    return p ? p->get() : nullptr;
}

}  // namespace seum
