#include "ir.h"

#include "error_reporter.h"

#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace seum::ir {

namespace {

// 함수 본문의 모든 `변수 X = …` 이름을 수집 (중첩 if/while/repeat 포함).
// v0.2e: 함수 안에선 모두 함수-스코프 지역. 블록 스코프 없음.
void collect_locals(const Stmt& s, std::vector<std::u32string>& acc,
                    std::unordered_set<std::u32string>& seen) {
    if (auto* vd = as_var_decl_stmt(s)) {
        if (seen.insert(vd->name).second) acc.push_back(vd->name);
        return;
    }
    if (auto* iff = as_if_stmt(s)) {
        for (const Stmt& sub : iff->then_body) collect_locals(sub, acc, seen);
        for (const Stmt& sub : iff->else_body) collect_locals(sub, acc, seen);
        return;
    }
    if (auto* rep = as_repeat_stmt(s)) {
        for (const Stmt& sub : rep->body) collect_locals(sub, acc, seen);
        return;
    }
    if (auto* wh = as_while_stmt(s)) {
        for (const Stmt& sub : wh->body) collect_locals(sub, acc, seen);
        return;
    }
    // ExprStmt/ImportStmt/ReturnStmt/FuncDeclStmt: 지역 도입 없음.
}

struct Lowerer {
    Function&                              fn;
    const std::unordered_map<std::u32string, std::uint32_t>& local_idx;
    const bool                             in_function;  // false = main

    Reg new_reg() { return fn.next_reg++; }
    Label here() const { return static_cast<Label>(fn.instrs.size()); }

    bool is_local(const std::u32string& name) const {
        return local_idx.find(name) != local_idx.end();
    }
    std::uint32_t idx_of(const std::u32string& name) const {
        return local_idx.at(name);
    }

    Reg lower_expr(const Expr& e) {
        if (auto* il = as_int_lit(e)) {
            Reg r = new_reg();
            Instr I; I.op = Op::CONST_INT; I.dst = r; I.int_val = il->value; I.pos = il->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* sl = as_string_lit(e)) {
            Reg r = new_reg();
            Instr I; I.op = Op::CONST_STR; I.dst = r; I.str_val = sl->value; I.pos = sl->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* bl = as_bool_lit(e)) {
            Reg r = new_reg();
            Instr I; I.op = bl->value ? Op::PUSH_TRUE : Op::PUSH_FALSE; I.dst = r; I.pos = bl->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* id = as_identifier(e)) {
            Reg r = new_reg();
            Instr I; I.dst = r; I.pos = id->pos;
            if (in_function && is_local(id->name)) {
                I.op = Op::LOAD_LOCAL; I.local_idx = idx_of(id->name);
            } else {
                I.op = Op::LOAD_GLOBAL; I.name = id->name;
            }
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* c = as_call(e)) {
            Reg callee = lower_expr(c->callee);
            std::vector<Reg> args;
            args.reserve(c->args.size());
            for (const Expr& a : c->args) args.push_back(lower_expr(a));
            Reg r = new_reg();
            Instr I; I.op = Op::CALL; I.dst = r; I.src = callee;
            I.arg_srcs = std::move(args); I.pos = c->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* u = as_unary(e)) {
            Reg operand = lower_expr(u->operand);
            Reg r = new_reg();
            Instr I; I.dst = r; I.src = operand; I.pos = u->pos;
            if      (u->op == U"!") I.op = Op::NOT;
            else if (u->op == U"-") I.op = Op::NEG;
            else                    I.op = Op::NOT;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* b = as_binary(e)) {
            return lower_binary(*b);
        }
        if (auto* us = as_unsafe(e)) {
            // v0.3e 결정 #58: 위험 { 식 } = inner 그대로 lower. marker.
            return lower_expr(us->inner);
        }
        if (auto* re = as_record_lit(e)) {
            // v0.4a-1 2b #69: 레코드 리터럴 → 키·값 push 후 MAKE_RECORD.
            for (const RecordField& f : re->fields) {
                Instr K; K.op = Op::CONST_STR; K.dst = new_reg();
                K.str_val = f.key; K.pos = re->pos;
                fn.instrs.push_back(std::move(K));
                lower_expr(f.value);
            }
            Reg r = new_reg();
            Instr I; I.op = Op::MAKE_RECORD; I.dst = r;
            I.int_val = static_cast<std::int64_t>(re->fields.size());
            I.pos = re->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        if (auto* me = as_member(e)) {
            // v0.4a-1 2b #79: 멤버 접근 → 대상 push 후 MEMBER_GET.
            lower_expr(me->target);
            Reg r = new_reg();
            Instr I; I.op = Op::MEMBER_GET; I.dst = r;
            I.str_val = me->field; I.pos = me->pos;
            fn.instrs.push_back(std::move(I));
            return r;
        }
        return new_reg();
    }

    Reg lower_binary(const BinaryExpr& B) {
        const std::u32string& op = B.op;
        if (op == U"&&" || op == U"||") {
            return lower_short_circuit(B, op == U"&&");
        }
        Reg l = lower_expr(B.lhs);
        Reg r = lower_expr(B.rhs);
        Reg d = new_reg();
        Instr I; I.dst = d; I.src = l; I.arg_srcs = {r}; I.pos = B.pos;
        if      (op == U"==") I.op = Op::EQ;
        else if (op == U"!=") I.op = Op::NE;
        else if (op == U"<")  I.op = Op::LT;
        else if (op == U">")  I.op = Op::GT;
        else if (op == U"<=") I.op = Op::LE;
        else if (op == U">=") I.op = Op::GE;
        else if (op == U"+")  I.op = Op::ADD;
        else if (op == U"-")  I.op = Op::SUB;
        else if (op == U"*")  I.op = Op::MUL;
        else if (op == U"/")  I.op = Op::DIV;
        else if (op == U"%")  I.op = Op::MOD;
        else raise(B.pos, U"알 수 없는 이항 연산자 (IR)");
        fn.instrs.push_back(std::move(I));
        return d;
    }

    Reg lower_short_circuit(const BinaryExpr& B, bool is_and) {
        Reg result = new_reg();
        lower_expr(B.lhs);
        Label jfz_idx = here();
        { Instr J; J.op = Op::JFZ; J.pos = B.pos; J.target = 0; fn.instrs.push_back(std::move(J)); }

        if (is_and) {
            lower_expr(B.rhs);
            Label jmp_idx = here();
            { Instr Jmp; Jmp.op = Op::JMP; Jmp.pos = B.pos; Jmp.target = 0; fn.instrs.push_back(std::move(Jmp)); }
            Label short_lbl = here();
            { Instr Pf; Pf.op = Op::PUSH_FALSE; Pf.dst = result; Pf.pos = B.pos; fn.instrs.push_back(std::move(Pf)); }
            Label end_lbl = here();
            fn.instrs[jfz_idx].target = short_lbl;
            fn.instrs[jmp_idx].target = end_lbl;
        } else {
            { Instr Pt; Pt.op = Op::PUSH_TRUE; Pt.dst = result; Pt.pos = B.pos; fn.instrs.push_back(std::move(Pt)); }
            Label jmp_idx = here();
            { Instr Jmp; Jmp.op = Op::JMP; Jmp.pos = B.pos; Jmp.target = 0; fn.instrs.push_back(std::move(Jmp)); }
            Label rhs_lbl = here();
            lower_expr(B.rhs);
            Label end_lbl = here();
            fn.instrs[jfz_idx].target = rhs_lbl;
            fn.instrs[jmp_idx].target = end_lbl;
        }
        return result;
    }

    void lower_stmt(const Stmt& s) {
        if (auto* imp = as_import_stmt(s)) {
            Instr I; I.op = Op::IMPORT; I.name = imp->module_name; I.pos = imp->pos;
            fn.instrs.push_back(std::move(I));
            return;
        }
        if (auto* vd = as_var_decl_stmt(s)) {
            Reg val = lower_expr(vd->value);
            Instr I; I.src = val; I.pos = vd->pos;
            if (in_function && is_local(vd->name)) {
                I.op = Op::STORE_LOCAL; I.local_idx = idx_of(vd->name);
            } else {
                I.op = Op::STORE_GLOBAL; I.name = vd->name;
            }
            fn.instrs.push_back(std::move(I));
            return;
        }
        if (auto* es = as_expr_stmt(s)) {
            Reg val = lower_expr(es->expr);
            Instr I; I.op = Op::DROP; I.src = val; I.pos = es->pos;
            fn.instrs.push_back(std::move(I));
            return;
        }
        if (auto* iff = as_if_stmt(s)) {
            lower_expr(iff->cond);
            Label jfz_idx = here();
            { Instr J; J.op = Op::JFZ; J.pos = iff->pos; J.target = 0; fn.instrs.push_back(std::move(J)); }
            for (const Stmt& sub : iff->then_body) lower_stmt(sub);
            const bool has_else = !iff->else_body.empty();
            if (has_else) {
                Label jmp_idx = here();
                { Instr Jmp; Jmp.op = Op::JMP; Jmp.pos = iff->pos; Jmp.target = 0; fn.instrs.push_back(std::move(Jmp)); }
                Label else_lbl = here();
                for (const Stmt& sub : iff->else_body) lower_stmt(sub);
                Label end_lbl = here();
                fn.instrs[jfz_idx].target = else_lbl;
                fn.instrs[jmp_idx].target = end_lbl;
            } else {
                Label end_lbl = here();
                fn.instrs[jfz_idx].target = end_lbl;
            }
            return;
        }
        if (auto* rep = as_repeat_stmt(s)) {
            lower_expr(rep->count);
            Label loop_lbl = here();
            { Instr I; I.op = Op::DUP; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            { Instr I; I.op = Op::CONST_INT; I.int_val = 0; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            { Instr I; I.op = Op::GT; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            Label jfz_idx = here();
            { Instr J; J.op = Op::JFZ; J.pos = rep->pos; J.target = 0; fn.instrs.push_back(std::move(J)); }
            for (const Stmt& sub : rep->body) lower_stmt(sub);
            { Instr I; I.op = Op::CONST_INT; I.int_val = 1; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            { Instr I; I.op = Op::SUB; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            { Instr I; I.op = Op::JMP; I.pos = rep->pos; I.target = loop_lbl; fn.instrs.push_back(std::move(I)); }
            Label end_lbl = here();
            fn.instrs[jfz_idx].target = end_lbl;
            { Instr I; I.op = Op::DROP; I.pos = rep->pos; fn.instrs.push_back(std::move(I)); }
            return;
        }
        if (auto* wh = as_while_stmt(s)) {
            Label loop_lbl = here();
            lower_expr(wh->cond);
            Label jfz_idx = here();
            { Instr J; J.op = Op::JFZ; J.pos = wh->pos; J.target = 0; fn.instrs.push_back(std::move(J)); }
            for (const Stmt& sub : wh->body) lower_stmt(sub);
            { Instr I; I.op = Op::JMP; I.pos = wh->pos; I.target = loop_lbl; fn.instrs.push_back(std::move(I)); }
            Label end_lbl = here();
            fn.instrs[jfz_idx].target = end_lbl;
            return;
        }
        if (auto* rt = as_return_stmt(s)) {
            if (rt->has_value) lower_expr(rt->value);
            else {
                Instr I; I.op = Op::PUSH_NIL; I.pos = rt->pos;
                fn.instrs.push_back(std::move(I));
            }
            Instr R; R.op = Op::RET; R.pos = rt->pos;
            fn.instrs.push_back(std::move(R));
            return;
        }
        // FuncDeclStmt 는 main 에서만 등장 (v0.2e). 별도 처리 — lower() 함수에서.
    }
};

void lower_function_body(Function& fn, const std::vector<Stmt>& body, bool in_function) {
    // 1) collect locals (params 이미 fn.local_names 에 있음)
    std::unordered_set<std::u32string> seen(fn.local_names.begin(), fn.local_names.end());
    for (const Stmt& s : body) collect_locals(s, fn.local_names, seen);

    // 2) build name → idx map
    std::unordered_map<std::u32string, std::uint32_t> idx_map;
    for (std::uint32_t i = 0; i < fn.local_names.size(); ++i) idx_map[fn.local_names[i]] = i;

    // 3) lower
    Lowerer L{fn, idx_map, in_function};
    for (const Stmt& s : body) L.lower_stmt(s);

    // 4) 함수면 마지막에 implicit PUSH_NIL + RET (이미 RET 으로 끝났어도 무해; VM 이 도달 안 함)
    if (in_function) {
        Position end_pos = body.empty() ? Position{} : Position{};
        Instr Pn; Pn.op = Op::PUSH_NIL; Pn.pos = end_pos; fn.instrs.push_back(std::move(Pn));
        Instr R; R.op = Op::RET; R.pos = end_pos; fn.instrs.push_back(std::move(R));
    }
}

const char32_t* op_name(Op op) {
    switch (op) {
        case Op::CONST_INT:    return U"정수상수";
        case Op::CONST_STR:    return U"문자열상수";
        case Op::PUSH_TRUE:    return U"참";
        case Op::PUSH_FALSE:   return U"거짓";
        case Op::PUSH_NIL:     return U"없음";
        case Op::LOAD_GLOBAL:  return U"전역가져오기";
        case Op::STORE_GLOBAL: return U"전역두기";
        case Op::LOAD_LOCAL:   return U"지역가져오기";
        case Op::STORE_LOCAL:  return U"지역두기";
        case Op::CALL:         return U"부르기";
        case Op::DROP:         return U"버리기";
        case Op::DUP:          return U"복사";
        case Op::IMPORT:       return U"가져오기";
        case Op::NOT:          return U"아닌";
        case Op::NEG:          return U"음수";
        case Op::EQ:           return U"같음";
        case Op::NE:           return U"다름";
        case Op::LT:           return U"작음";
        case Op::GT:           return U"큼";
        case Op::LE:           return U"작거나같음";
        case Op::GE:           return U"크거나같음";
        case Op::ADD:          return U"더하기";
        case Op::SUB:          return U"빼기";
        case Op::MUL:          return U"곱하기";
        case Op::DIV:          return U"나누기";
        case Op::MOD:          return U"나머지";
        case Op::JMP:          return U"뛰기";
        case Op::JFZ:          return U"거짓이면뛰기";
        case Op::RET:          return U"돌려주기";
        case Op::MAKE_RECORD:  return U"레코드만들기";
        case Op::MEMBER_GET:   return U"멤버가져오기";
    }
    return U"???";
}

std::u32string reg_str(Reg r) {
    std::u32string out = U"%";
    std::string s = std::to_string(r);
    out.append(s.begin(), s.end());
    return out;
}
std::u32string int_str(std::int64_t v) {
    std::string s = std::to_string(v);
    return std::u32string(s.begin(), s.end());
}
std::u32string label_str(Label v) {
    std::u32string out = U"L";
    std::string s = std::to_string(v);
    out.append(s.begin(), s.end());
    return out;
}
std::u32string quote(const std::u32string& s) {
    std::u32string out; out += U"\""; out += s; out += U"\""; return out;
}

}  // namespace

Program lower(const seum::Program& ast) {
    Program prog;
    prog.main.name = U"";

    // 1) Top-level FuncDeclStmt 각각을 별도 Function 으로 lower.
    for (const Stmt& s : ast.statements) {
        if (auto* fd = as_func_decl(s)) {
            Function f;
            f.name        = fd->name;
            f.params      = fd->params;
            f.local_names = fd->params;
            f.is_getter   = fd->is_getter;     // v0.3e #59
            lower_function_body(f, fd->body, /*in_function=*/true);
            prog.functions.push_back(std::move(f));
        }
    }
    // 2) Main = top-level 중 FuncDeclStmt 가 *아닌* 것들.
    std::vector<Stmt> main_body_dummy;  // 안 쓰임 — 직접 lower
    std::unordered_set<std::u32string> seen;
    // main 의 locals 도 수집해 둠 — 단 v0.2e 의 main 은 글로벌만 (locals 사용 안 함).
    // local_names 는 비워두고 in_function=false 로 lower.
    {
        std::unordered_map<std::u32string, std::uint32_t> empty_idx;
        Lowerer L{prog.main, empty_idx, /*in_function=*/false};
        for (const Stmt& s : ast.statements) {
            if (is_func_decl(s)) continue;
            L.lower_stmt(s);
        }
    }
    return prog;
}

std::u32string disassemble(const Function& fn) {
    std::u32string out;
    for (Label i = 0; i < fn.instrs.size(); ++i) {
        const Instr& I = fn.instrs[i];
        out += label_str(i); out += U": ";
        switch (I.op) {
            case Op::CONST_INT:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += int_str(I.int_val); break;
            case Op::CONST_STR:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += quote(I.str_val); break;
            case Op::PUSH_TRUE:
            case Op::PUSH_FALSE:
            case Op::PUSH_NIL:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); break;
            case Op::LOAD_GLOBAL:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += quote(I.name); break;
            case Op::STORE_GLOBAL:
                out += op_name(I.op); out += U" "; out += quote(I.name); out += U", "; out += reg_str(I.src); break;
            case Op::LOAD_LOCAL:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += int_str(static_cast<std::int64_t>(I.local_idx)); break;
            case Op::STORE_LOCAL:
                out += op_name(I.op); out += U" "; out += int_str(static_cast<std::int64_t>(I.local_idx)); out += U", "; out += reg_str(I.src); break;
            case Op::CALL: {
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += reg_str(I.src); out += U" (";
                for (std::size_t k = 0; k < I.arg_srcs.size(); ++k) {
                    if (k) out += U", ";
                    out += reg_str(I.arg_srcs[k]);
                }
                out += U")";
                break;
            }
            case Op::DROP:
                out += op_name(I.op); out += U" "; out += reg_str(I.src); break;
            case Op::DUP:
                out += op_name(I.op); break;
            case Op::IMPORT:
                out += op_name(I.op); out += U" "; out += quote(I.name); break;
            case Op::NOT:
            case Op::NEG:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += reg_str(I.src); break;
            case Op::EQ: case Op::NE: case Op::LT:
            case Op::GT: case Op::LE: case Op::GE:
            case Op::ADD: case Op::SUB: case Op::MUL:
            case Op::DIV: case Op::MOD:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op); out += U" "; out += reg_str(I.src);
                out += U", "; out += reg_str(I.arg_srcs[0]); break;
            case Op::JMP:
            case Op::JFZ:
                out += op_name(I.op); out += U" "; out += label_str(I.target); break;
            case Op::RET:
                out += op_name(I.op); break;
            case Op::MAKE_RECORD:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op);
                out += U" "; out += int_str(I.int_val); break;
            case Op::MEMBER_GET:
                out += reg_str(I.dst); out += U" = "; out += op_name(I.op);
                out += U" "; out += quote(I.str_val); break;
        }
        out += U"\n";
    }
    return out;
}

std::u32string disassemble(const Program& prog) {
    std::u32string out;
    out += U"=== main ===\n";
    out += disassemble(prog.main);
    for (const Function& fn : prog.functions) {
        out += U"=== 함수 ";
        out += fn.name;
        out += U" (";
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out += U", ";
            out += fn.params[i];
        }
        out += U") ===\n";
        out += disassemble(fn);
    }
    return out;
}

}  // namespace seum::ir
