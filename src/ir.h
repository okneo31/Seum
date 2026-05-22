#pragma once

#include "ast.h"
#include "common.h"

namespace seum::ir {

using Reg = std::uint32_t;
using Label = std::uint32_t;

enum class Op : std::uint8_t {
    CONST_INT,
    CONST_STR,
    PUSH_TRUE,
    PUSH_FALSE,
    PUSH_NIL,            // v0.2e: `돌려주기.` 의 implicit 없음
    LOAD_GLOBAL,
    STORE_GLOBAL,
    LOAD_LOCAL,          // v0.2e: 지역(매개변수+함수 본문 var)
    STORE_LOCAL,
    CALL,
    DROP,
    DUP,
    IMPORT,
    NOT,
    NEG,
    EQ, NE, LT, GT, LE, GE,
    ADD, SUB, MUL, DIV, MOD,
    JMP,
    JFZ,
    RET,                 // v0.2e: 현재 프레임 종료, top 을 호출자에게 반환
    MAKE_RECORD,         // v0.4a-1 2b: int_val = 필드 수. 스택 키·값 2N개 → 레코드
    MEMBER_GET,          // v0.4a-1 2b: str_val = 필드 이름. 스택 top 레코드의 필드
};

struct Instr {
    Op                          op;
    Reg                         dst{};
    std::int64_t                int_val{0};
    std::u32string              str_val;
    std::u32string              name;
    Reg                         src{};
    std::vector<Reg>            arg_srcs;
    Label                       target{0};
    std::uint32_t               local_idx{0};   // v0.2e: LOAD_LOCAL/STORE_LOCAL
    Position                    pos;
};

// v0.2e: 한 함수의 IR (main 포함). 결정 F9.
struct Function {
    std::u32string               name;          // "" = main
    std::vector<std::u32string>  params;        // 매개변수 이름 (locals 의 첫 N개)
    std::vector<std::u32string>  local_names;   // params 포함, 그 뒤 본문 변수
    std::vector<Instr>           instrs;
    Reg                          next_reg{0};
    bool                         is_getter{false};   // v0.3e #59
};

struct Program {
    Function              main;
    std::vector<Function> functions;
};

Program lower(const seum::Program& ast);
std::u32string disassemble(const Function& fn);
std::u32string disassemble(const Program& prog);

}  // namespace seum::ir
