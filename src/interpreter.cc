#include "interpreter.h"

#include "error_reporter.h"

namespace seum {

namespace {

// 함수 본문에서 `돌려주기` 가 흐름 단축에 쓰는 신호. 호출 측에서만 catch.
struct ReturnSignal { Value value; };

Value eval_expr(const Expr& e, Environment& env);
void  eval_stmt(const Stmt& s, Environment& env);

Value eval_call(const CallExpr& call, Environment& env) {
    Value callee = eval_expr(call.callee, env);
    const CallableValue* callable = as_callable(callee);
    if (!callable) {
        raise(call.pos, U"호출 불가능한 값을 호출하려 했습니다");
    }
    std::vector<Value> args;
    args.reserve(call.args.size());
    for (const Expr& a : call.args) {
        args.push_back(eval_expr(a, env));
    }
    return callable->fn(args, call.pos);
}

bool eval_truthy_bool(const Value& v, Position pos) {
    auto* b = as_bool(v);
    if (!b) {
        raise(pos, U"진리값이 아닙니다 (참 또는 거짓이 필요)");
    }
    return *b;
}

// `==`/`!=` 는 int·str·bool 모두 비교 가능. 두 값의 종류가 다르면 다른 것으로 간주.
bool values_equal(const Value& a, const Value& b) {
    if (auto pa = as_bool(a))   { if (auto pb = as_bool(b))   return *pa == *pb; return false; }
    if (auto pa = as_int(a))    { if (auto pb = as_int(b))    return *pa == *pb; return false; }
    if (auto pa = as_string(a)) { if (auto pb = as_string(b)) return *pa == *pb; return false; }
    // 함수값·레코드 등 합성/참조 타입은 동등성 미정의 — 항상 거짓 (결정 94).
    return false;
}

// v0.2c: `+` 다형 — string+string 결합, int+int 산술합. 혼합은 타입 에러 (#31).
// v0.4a-2: 필드 복합대입 `+=` 도 이 의미를 공유.
Value add_values(const Value& l, const Value& r, Position pos) {
    if (auto ls = as_string(l)) {
        auto rs = as_string(r);
        if (!rs) raise(pos, U"+ 양변이 모두 문자열이어야 결합됩니다");
        return make_string(*ls + *rs);
    }
    // v0.4a-5 #64: 자산 + 자산 — 같은 통화끼리만, 결과 음수 금지.
    if (auto la = as_asset(l)) {
        auto ra = as_asset(r);
        if (!ra) raise(pos, U"자산과 자산만 더할 수 있습니다");
        if (la->currency != ra->currency) {
            raise(pos, U"통화가 다른 자산은 더할 수 없습니다");
        }
        std::int64_t sum = la->amount + ra->amount;
        if (sum < 0) raise(pos, U"자산은 음수가 될 수 없습니다");
        return make_asset(la->currency, sum);
    }
    auto li = as_int(l);
    auto ri = as_int(r);
    if (!li || !ri) raise(pos, U"+ 는 정수 또는 문자열에만 적용 가능합니다");
    return make_int(*li + *ri);
}

Value eval_binary(const BinaryExpr& B, Environment& env) {
    const std::u32string& op = B.op;
    // 단락 평가: && / ||
    if (op == U"&&") {
        Value l = eval_expr(B.lhs, env);
        bool lb = eval_truthy_bool(l, B.pos);
        if (!lb) return make_bool(false);
        Value r = eval_expr(B.rhs, env);
        return make_bool(eval_truthy_bool(r, B.pos));
    }
    if (op == U"||") {
        Value l = eval_expr(B.lhs, env);
        bool lb = eval_truthy_bool(l, B.pos);
        if (lb) return make_bool(true);
        Value r = eval_expr(B.rhs, env);
        return make_bool(eval_truthy_bool(r, B.pos));
    }

    Value l = eval_expr(B.lhs, env);
    Value r = eval_expr(B.rhs, env);

    if (op == U"==") return make_bool( values_equal(l, r));
    if (op == U"!=") return make_bool(!values_equal(l, r));

    // v0.2c: `+` 다형 — add_values 로 추출 (v0.4a-2 `+=` 와 공유).
    if (op == U"+") return add_values(l, r, B.pos);

    // 그 외 산술/비교 — int 전용
    auto* li = as_int(l);
    auto* ri = as_int(r);
    if (!li || !ri) {
        raise(B.pos, U"비교/산술 연산은 정수에만 적용 가능합니다");
    }

    // v0.2c 산술
    if (op == U"-") return make_int(*li - *ri);
    if (op == U"*") return make_int(*li * *ri);
    if (op == U"/") {
        if (*ri == 0) raise(B.pos, U"0으로 나눌 수 없습니다");
        return make_int(*li / *ri);
    }
    if (op == U"%") {
        if (*ri == 0) raise(B.pos, U"0으로 나머지 연산을 할 수 없습니다");
        return make_int(*li % *ri);
    }

    if (op == U"<")  return make_bool(*li <  *ri);
    if (op == U">")  return make_bool(*li >  *ri);
    if (op == U"<=") return make_bool(*li <= *ri);
    if (op == U">=") return make_bool(*li >= *ri);

    raise(B.pos, U"알 수 없는 이항 연산자");
}

Value eval_unary(const UnaryExpr& U_, Environment& env) {
    Value v = eval_expr(U_.operand, env);
    if (U_.op == U"!") {
        return make_bool(!eval_truthy_bool(v, U_.pos));
    }
    if (U_.op == U"-") {
        auto* i = as_int(v);
        if (!i) raise(U_.pos, U"단항 - 는 정수에만 적용 가능합니다");
        return make_int(-(*i));
    }
    raise(U_.pos, U"알 수 없는 단항 연산자");
}

Value eval_expr(const Expr& e, Environment& env) {
    if (auto* il = as_int_lit(e))    return make_int(il->value);
    if (auto* sl = as_string_lit(e)) return make_string(sl->value);
    if (auto* bl = as_bool_lit(e))   return make_bool(bl->value);
    if (auto* id = as_identifier(e)) {
        auto found = env.get(id->name);
        if (!found) {
            std::u32string msg = U"이름을 찾을 수 없습니다: ";
            msg += id->name;
            raise(id->pos, msg);
        }
        return *found;
    }
    if (auto* c = as_call(e))   return eval_call(*c, env);
    if (auto* b = as_binary(e)) return eval_binary(*b, env);
    if (auto* u = as_unary(e))  return eval_unary(*u, env);
    if (auto* us = as_unsafe(e)) {
        // v0.3e: 위험 { 식 } = 식 그대로 평가. 의미적 marker.
        // v0.5+ 에서 sandbox/권한 검사 본격 도입.
        return eval_expr(us->inner, env);
    }
    if (auto* re = as_record_lit(e)) {
        // v0.4a-1 #69: 레코드 리터럴 → RecordValue (필드 순서 보존).
        // 255 한도 — VM(bytecode 1B 피연산자)과 동일 (양 경로 의무 #32).
        if (re->fields.size() > 255) {
            raise(re->pos, U"레코드 필드는 255개를 넘을 수 없습니다");
        }
        auto rec = std::make_shared<RecordValue>();
        rec->fields.reserve(re->fields.size());
        for (const RecordField& f : re->fields) {
            rec->fields.emplace_back(f.key, eval_expr(f.value, env));
        }
        return Value{rec};
    }
    if (auto* me = as_member(e)) {
        // v0.4a-1 #79: 멤버 접근 — v0.4a 는 레코드 대상만.
        Value target = eval_expr(me->target, env);
        RecordValue* rec = as_record(target);
        if (!rec) {
            raise(me->pos, U"멤버 접근은 레코드에만 가능합니다");
        }
        const Value* fv = rec->get(me->field);
        if (!fv) {
            std::u32string msg = U"레코드에 없는 멤버: ";
            msg += me->field;
            raise(me->pos, msg);
        }
        return *fv;
    }
    return make_none();  // unreachable
}

void eval_stmt(const Stmt& s, Environment& env) {
    if (auto* imp = as_import_stmt(s)) {
        if (!env.import_module(imp->module_name)) {
            std::u32string msg = U"알 수 없는 그릇: ";
            msg += imp->module_name;
            raise(imp->pos, msg);
        }
        return;
    }
    if (auto* vd = as_var_decl_stmt(s)) {
        Value v = eval_expr(vd->value, env);
        env.define(vd->name, std::move(v));
        return;
    }
    if (auto* es = as_expr_stmt(s)) {
        eval_expr(es->expr, env);
        return;
    }
    if (auto* asn = as_assign_stmt(s)) {
        // v0.4a-2 #81: 필드 대입. target 은 MemberExpr (parser 가 보장).
        const MemberExpr* me = as_member(asn->target);
        if (!me) raise(asn->pos, U"대입 대상은 레코드/계약 필드여야 합니다");
        Value parent = eval_expr(me->target, env);
        RecordValue* rec = as_record(parent);
        if (!rec) raise(me->pos, U"멤버 대입은 레코드에만 가능합니다");
        Value rhs = eval_expr(asn->value, env);   // slot 잡기 전에 rhs 먼저 평가
        Value* slot = rec->get(me->field);
        if (!slot) {
            std::u32string msg = U"레코드에 없는 멤버: "; msg += me->field;
            raise(me->pos, msg);
        }
        if (asn->op == U"+=") {
            *slot = add_values(*slot, rhs, asn->pos);
        } else {
            *slot = std::move(rhs);
        }
        return;
    }
    if (auto* iff = as_if_stmt(s)) {
        Value cond = eval_expr(iff->cond, env);
        bool b = eval_truthy_bool(cond, iff->pos);
        const std::vector<Stmt>& body = b ? iff->then_body : iff->else_body;
        for (const Stmt& sub : body) {
            eval_stmt(sub, env);
        }
        return;
    }
    if (auto* rep = as_repeat_stmt(s)) {
        Value v = eval_expr(rep->count, env);
        auto* n = as_int(v);
        if (!n) raise(rep->pos, U"반복 횟수는 정수여야 합니다");
        for (std::int64_t i = 0; i < *n; ++i) {
            for (const Stmt& sub : rep->body) {
                eval_stmt(sub, env);
            }
        }
        return;
    }
    if (auto* wh = as_while_stmt(s)) {
        while (true) {
            Value c = eval_expr(wh->cond, env);
            bool b = eval_truthy_bool(c, wh->pos);
            if (!b) break;
            for (const Stmt& sub : wh->body) {
                eval_stmt(sub, env);
            }
        }
        return;
    }
    if (auto* fd = as_func_decl(s)) {
        const Environment* parent = &env;
        const std::vector<std::u32string>* params_ptr = &fd->params;
        const std::vector<Stmt>* body_ptr = &fd->body;

        if (fd->is_getter) {
            // v0.3e #59: 지연값 = GetterValue (env.get 시 자동 발화).
            // 매개변수 0개 의무 (parser 가 검증).
            auto getter = std::make_shared<GetterValue>();
            getter->name = fd->name;
            getter->fn = [parent, body_ptr]() -> Value {
                Environment frame(parent);
                try {
                    for (const Stmt& sub : *body_ptr) eval_stmt(sub, frame);
                } catch (ReturnSignal& rs) {
                    return std::move(rs.value);
                }
                return make_none();
            };
            env.define(fd->name, Value{getter});
            return;
        }

        auto callable = std::make_shared<CallableValue>();
        callable->name = fd->name;
        callable->fn = [parent, params_ptr, body_ptr](std::vector<Value>& args, Position call_pos) -> Value {
            if (args.size() != params_ptr->size()) {
                auto to_u32 = [](std::size_t n) {
                    std::string s = std::to_string(n);
                    return std::u32string(s.begin(), s.end());
                };
                std::u32string msg = U"인자 개수 불일치: 기대 ";
                msg += to_u32(params_ptr->size());
                msg += U" / 받음 ";
                msg += to_u32(args.size());
                raise(call_pos, msg);
            }
            Environment frame(parent);
            for (std::size_t i = 0; i < params_ptr->size(); ++i) {
                frame.define((*params_ptr)[i], std::move(args[i]));
            }
            try {
                for (const Stmt& sub : *body_ptr) eval_stmt(sub, frame);
            } catch (ReturnSignal& rs) {
                return std::move(rs.value);
            }
            return make_none();
        };
        env.define(fd->name, Value{callable});
        return;
    }
    if (auto* rt = as_return_stmt(s)) {
        Value v = rt->has_value ? eval_expr(rt->value, env) : make_none();
        throw ReturnSignal{std::move(v)};
    }
}

}  // namespace

void evaluate(const Program& program, Environment& env) {
    for (const Stmt& s : program.statements) {
        eval_stmt(s, env);
    }
}

Value evaluate_expr(const Expr& e, Environment& env) {
    return eval_expr(e, env);
}

}  // namespace seum
