#include "vm.h"

#include "error_reporter.h"

#include <cstdint>
#include <vector>

namespace seum::vm {

namespace {

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool values_equal(const Value& a, const Value& b) {
    if (auto pa = as_bool(a))   { if (auto pb = as_bool(b))   return *pa == *pb; return false; }
    if (auto pa = as_int(a))    { if (auto pb = as_int(b))    return *pa == *pb; return false; }
    if (auto pa = as_string(a)) { if (auto pb = as_string(b)) return *pa == *pb; return false; }
    // 함수값·레코드 등 합성/참조 타입은 동등성 미정의 — 항상 거짓 (결정 94).
    return false;
}

// 단일 함수/main Chunk 실행.
// is_function: true 면 RET 시 top 을 반환값으로 돌려줌. false (main) 이면 HALT 만 종료.
// 함수 호출 시 새 locals 배열 생성, 매개변수 배치.
struct Runner {
    const bytecode::Program& prog;
    Environment&             env;

    // 사용자 함수 호출 — 새 frame.
    Value call_user_function(const bytecode::Function& fn, std::vector<Value>& args, Position call_pos) {
        if (args.size() != fn.params.size()) {
            auto to_u32 = [](std::size_t n) { std::string s = std::to_string(n); return std::u32string(s.begin(), s.end()); };
            std::u32string msg = U"인자 개수 불일치: 기대 ";
            msg += to_u32(fn.params.size()); msg += U" / 받음 "; msg += to_u32(args.size());
            raise(call_pos, msg);
        }
        std::vector<Value> locals(fn.local_count);
        for (std::size_t i = 0; i < args.size(); ++i) locals[i] = std::move(args[i]);
        return run_chunk(fn.chunk, locals, /*is_function=*/true);
    }

    Value run_chunk(const bytecode::Chunk& chunk,
                    std::vector<Value>& locals,
                    bool is_function) {
        using bytecode::Opcode;

        std::vector<Value> stack;
        stack.reserve(64);
        std::size_t pc = 0;

        auto pos_at = [&](std::size_t at) -> Position {
            if (at < chunk.positions.size()) return chunk.positions[at];
            return {};
        };

        while (pc < chunk.bytes.size()) {
            auto op = static_cast<Opcode>(chunk.bytes[pc]);
            Position pos = pos_at(pc);

            switch (op) {
                case Opcode::CONST_INT: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    stack.push_back(make_int(chunk.int_pool[idx]));
                    pc += 5; break;
                }
                case Opcode::CONST_STR: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    stack.push_back(make_string(chunk.str_pool[idx]));
                    pc += 5; break;
                }
                case Opcode::PUSH_TRUE:  stack.push_back(make_bool(true));  pc += 1; break;
                case Opcode::PUSH_FALSE: stack.push_back(make_bool(false)); pc += 1; break;
                case Opcode::PUSH_NIL:   stack.push_back(make_none());      pc += 1; break;
                case Opcode::LOAD_GLOBAL: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    const std::u32string& name = chunk.name_pool[idx];
                    auto found = env.get(name);
                    if (!found) {
                        std::u32string msg = U"이름을 찾을 수 없습니다: "; msg += name;
                        raise(pos, msg);
                    }
                    stack.push_back(std::move(*found));
                    pc += 5; break;
                }
                case Opcode::STORE_GLOBAL: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    const std::u32string& name = chunk.name_pool[idx];
                    Value v = std::move(stack.back()); stack.pop_back();
                    env.define(name, std::move(v));
                    pc += 5; break;
                }
                case Opcode::LOAD_LOCAL: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    if (idx >= locals.size()) raise(pos, U"VM 내부 오류: 지역 인덱스 초과");
                    stack.push_back(locals[idx]);
                    pc += 5; break;
                }
                case Opcode::STORE_LOCAL: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    if (idx >= locals.size()) raise(pos, U"VM 내부 오류: 지역 인덱스 초과");
                    locals[idx] = std::move(stack.back()); stack.pop_back();
                    pc += 5; break;
                }
                case Opcode::CALL: {
                    std::uint8_t argc = chunk.bytes[pc + 1];
                    if (stack.size() < static_cast<std::size_t>(argc) + 1) {
                        raise(pos, U"VM 내부 오류: 호출 스택 부족");
                    }
                    std::vector<Value> args(argc);
                    for (int i = argc - 1; i >= 0; --i) {
                        args[i] = std::move(stack.back()); stack.pop_back();
                    }
                    Value callee = std::move(stack.back()); stack.pop_back();
                    const CallableValue* callable = as_callable(callee);
                    if (!callable) raise(pos, U"호출 불가능한 값을 호출하려 했습니다");
                    Value result = callable->fn(args, pos);
                    stack.push_back(std::move(result));
                    pc += 2; break;
                }
                case Opcode::RET: {
                    if (!is_function) raise(pos, U"VM 내부 오류: main 에서 RET");
                    if (stack.empty()) return make_none();
                    Value rv = std::move(stack.back()); stack.pop_back();
                    return rv;
                }
                case Opcode::DROP: {
                    if (stack.empty()) raise(pos, U"VM 내부 오류: 빈 스택에서 버리기");
                    stack.pop_back(); pc += 1; break;
                }
                case Opcode::DUP: {
                    if (stack.empty()) raise(pos, U"VM 내부 오류: 빈 스택에서 복사");
                    stack.push_back(stack.back()); pc += 1; break;
                }
                case Opcode::IMPORT: {
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    const std::u32string& name = chunk.name_pool[idx];
                    if (!env.import_module(name)) {
                        std::u32string msg = U"알 수 없는 그릇: "; msg += name;
                        raise(pos, msg);
                    }
                    pc += 5; break;
                }
                case Opcode::NOT: {
                    if (stack.empty()) raise(pos, U"VM: NOT 빈 스택");
                    Value v = std::move(stack.back()); stack.pop_back();
                    auto* b = as_bool(v);
                    if (!b) raise(pos, U"진리값이 아닙니다 (참 또는 거짓이 필요)");
                    stack.push_back(make_bool(!*b));
                    pc += 1; break;
                }
                case Opcode::NEG: {
                    if (stack.empty()) raise(pos, U"VM: NEG 빈 스택");
                    Value v = std::move(stack.back()); stack.pop_back();
                    auto* i = as_int(v);
                    if (!i) raise(pos, U"단항 - 는 정수에만 적용 가능합니다");
                    stack.push_back(make_int(-(*i)));
                    pc += 1; break;
                }
                case Opcode::ADD: {
                    Value b = std::move(stack.back()); stack.pop_back();
                    Value a = std::move(stack.back()); stack.pop_back();
                    if (auto as_ = as_string(a)) {
                        auto bs = as_string(b);
                        if (!bs) raise(pos, U"+ 양변이 모두 문자열이어야 결합됩니다");
                        stack.push_back(make_string(*as_ + *bs));
                    } else {
                        auto ai = as_int(a); auto bi = as_int(b);
                        if (!ai || !bi) raise(pos, U"+ 는 정수 또는 문자열에만 적용 가능합니다");
                        stack.push_back(make_int(*ai + *bi));
                    }
                    pc += 1; break;
                }
                case Opcode::SUB: case Opcode::MUL:
                case Opcode::DIV: case Opcode::MOD: {
                    Value b = std::move(stack.back()); stack.pop_back();
                    Value a = std::move(stack.back()); stack.pop_back();
                    auto* ai = as_int(a); auto* bi = as_int(b);
                    if (!ai || !bi) raise(pos, U"산술 연산은 정수에만 적용 가능합니다");
                    std::int64_t r = 0;
                    switch (op) {
                        case Opcode::SUB: r = *ai - *bi; break;
                        case Opcode::MUL: r = *ai * *bi; break;
                        case Opcode::DIV:
                            if (*bi == 0) raise(pos, U"0으로 나눌 수 없습니다");
                            r = *ai / *bi; break;
                        case Opcode::MOD:
                            if (*bi == 0) raise(pos, U"0으로 나머지 연산을 할 수 없습니다");
                            r = *ai % *bi; break;
                        default: break;
                    }
                    stack.push_back(make_int(r));
                    pc += 1; break;
                }
                case Opcode::EQ:
                case Opcode::NE: {
                    Value b = std::move(stack.back()); stack.pop_back();
                    Value a = std::move(stack.back()); stack.pop_back();
                    bool eq = values_equal(a, b);
                    stack.push_back(make_bool(op == Opcode::EQ ? eq : !eq));
                    pc += 1; break;
                }
                case Opcode::LT: case Opcode::GT:
                case Opcode::LE: case Opcode::GE: {
                    Value b = std::move(stack.back()); stack.pop_back();
                    Value a = std::move(stack.back()); stack.pop_back();
                    auto* ai = as_int(a); auto* bi = as_int(b);
                    if (!ai || !bi) raise(pos, U"비교 연산은 정수에만 적용 가능합니다");
                    bool r = false;
                    switch (op) {
                        case Opcode::LT: r = (*ai <  *bi); break;
                        case Opcode::GT: r = (*ai >  *bi); break;
                        case Opcode::LE: r = (*ai <= *bi); break;
                        case Opcode::GE: r = (*ai >= *bi); break;
                        default: break;
                    }
                    stack.push_back(make_bool(r));
                    pc += 1; break;
                }
                case Opcode::JMP: {
                    std::uint32_t target = read_u32_le(&chunk.bytes[pc + 1]);
                    pc = target; break;
                }
                case Opcode::JFZ: {
                    std::uint32_t target = read_u32_le(&chunk.bytes[pc + 1]);
                    if (stack.empty()) raise(pos, U"VM: JFZ 빈 스택");
                    Value v = std::move(stack.back()); stack.pop_back();
                    auto* b = as_bool(v);
                    if (!b) raise(pos, U"진리값이 아닙니다 (참 또는 거짓이 필요)");
                    if (!*b) pc = target;
                    else     pc += 5;
                    break;
                }
                case Opcode::MAKE_RECORD: {
                    // v0.4a-1 2b #69: 스택 키·값 2N개 → RecordValue.
                    // 스택 bottom→top = key0,val0,key1,val1,…
                    std::uint8_t n = chunk.bytes[pc + 1];
                    if (stack.size() < static_cast<std::size_t>(n) * 2) {
                        raise(pos, U"VM 내부 오류: 레코드 스택 부족");
                    }
                    auto rec = std::make_shared<RecordValue>();
                    rec->fields.resize(n);
                    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
                        Value val = std::move(stack.back()); stack.pop_back();
                        Value key = std::move(stack.back()); stack.pop_back();
                        auto* ks = as_string(key);
                        if (!ks) raise(pos, U"VM 내부 오류: 레코드 키가 문자열이 아님");
                        rec->fields[i] = { *ks, std::move(val) };
                    }
                    stack.push_back(Value{rec});
                    pc += 2; break;
                }
                case Opcode::MEMBER_GET: {
                    // v0.4a-1 2b #79: 스택 top 레코드의 필드 조회.
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    const std::u32string& field = chunk.str_pool[idx];
                    if (stack.empty()) raise(pos, U"VM 내부 오류: 멤버 접근 빈 스택");
                    Value target = std::move(stack.back()); stack.pop_back();
                    RecordValue* rec = as_record(target);
                    if (!rec) raise(pos, U"멤버 접근은 레코드에만 가능합니다");
                    const Value* fv = rec->get(field);
                    if (!fv) {
                        std::u32string msg = U"레코드에 없는 멤버: "; msg += field;
                        raise(pos, msg);
                    }
                    stack.push_back(*fv);
                    pc += 5; break;
                }
                case Opcode::MEMBER_SET: {
                    // v0.4a-2 #81: 스택 [record, value] → 레코드 필드 설정.
                    std::uint32_t idx = read_u32_le(&chunk.bytes[pc + 1]);
                    const std::u32string& field = chunk.str_pool[idx];
                    if (stack.size() < 2) raise(pos, U"VM 내부 오류: 멤버 대입 스택 부족");
                    Value rhs    = std::move(stack.back()); stack.pop_back();
                    Value target = std::move(stack.back()); stack.pop_back();
                    RecordValue* rec = as_record(target);
                    if (!rec) raise(pos, U"멤버 대입은 레코드에만 가능합니다");
                    Value* slot = rec->get(field);
                    if (!slot) {
                        std::u32string msg = U"레코드에 없는 멤버: "; msg += field;
                        raise(pos, msg);
                    }
                    *slot = std::move(rhs);
                    pc += 5; break;
                }
                case Opcode::HALT:
                    return make_none();
            }
        }
        return make_none();
    }
};

}  // namespace

void register_program(const bytecode::Program& prog, Environment& env) {
    // prog 와 env reference 캡처. 호출 시점에 *새 Runner* 즉석 생성.
    // 라이프타임: prog 는 caller (resolver / static cache) 가 보장.
    for (const bytecode::Function& fn : prog.functions) {
        const bytecode::Function* fn_ptr  = &fn;
        const bytecode::Program*  prog_ptr = &prog;
        Environment*              env_ptr = &env;
        if (fn.is_getter) {
            auto getter = std::make_shared<GetterValue>();
            getter->name = fn.name;
            getter->fn = [prog_ptr, fn_ptr, env_ptr]() -> Value {
                Runner runner{*prog_ptr, *env_ptr};
                std::vector<Value> no_args;
                return runner.call_user_function(*fn_ptr, no_args, Position{});
            };
            env.define(fn.name, Value{getter});
            continue;
        }
        auto callable = std::make_shared<CallableValue>();
        callable->name = fn.name;
        callable->fn = [prog_ptr, fn_ptr, env_ptr](std::vector<Value>& args, Position call_pos) -> Value {
            Runner runner{*prog_ptr, *env_ptr};
            return runner.call_user_function(*fn_ptr, args, call_pos);
        };
        env.define(fn.name, Value{callable});
    }
}

int run(const bytecode::Program& prog, Environment& env) {
    register_program(prog, env);
    Runner runner{prog, env};
    std::vector<Value> main_locals(prog.main.local_count);
    runner.run_chunk(prog.main.chunk, main_locals, /*is_function=*/false);
    return 0;
}

}  // namespace seum::vm
