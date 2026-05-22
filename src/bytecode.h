#pragma once

#include "common.h"

namespace seum::ir { struct Program; struct Function; }

namespace seum::bytecode {

enum class Opcode : std::uint8_t {
    CONST_INT     = 0x01,
    CONST_STR     = 0x02,
    PUSH_TRUE     = 0x03,
    PUSH_FALSE    = 0x04,
    PUSH_NIL      = 0x05,   // v0.2e
    LOAD_GLOBAL   = 0x10,
    STORE_GLOBAL  = 0x11,
    LOAD_LOCAL    = 0x12,   // v0.2e: operand 4B local_idx
    STORE_LOCAL   = 0x13,   // v0.2e
    CALL          = 0x20,
    RET           = 0x21,   // v0.2e: frame 종료, top 을 호출자에게
    DROP          = 0x30,
    DUP           = 0x31,
    IMPORT        = 0x40,
    NOT           = 0x50,
    EQ            = 0x51,
    NE            = 0x52,
    LT            = 0x53,
    GT            = 0x54,
    LE            = 0x55,
    GE            = 0x56,
    JMP           = 0x60,
    JFZ           = 0x61,
    NEG           = 0x70,
    ADD           = 0x71,
    SUB           = 0x72,
    MUL           = 0x73,
    DIV           = 0x74,
    MOD           = 0x75,
    MAKE_RECORD   = 0x80,   // v0.4a-1 2b: operand 1B = 필드 수
    MEMBER_GET    = 0x81,   // v0.4a-1 2b: operand 4B = str_pool 인덱스
    HALT          = 0xFF,
};

struct Chunk {
    std::vector<std::uint8_t>      bytes;
    std::vector<std::int64_t>      int_pool;
    std::vector<std::u32string>    str_pool;
    std::vector<std::u32string>    name_pool;
    std::vector<Position>          positions;  // 평행 (operand 슬롯도 채워짐)
};

// v0.2e: 한 함수의 바이트코드 묶음.
struct Function {
    std::u32string                name;          // "" = main
    std::vector<std::u32string>   params;
    std::uint32_t                 local_count{0};
    Chunk                         chunk;
    bool                          is_getter{false};  // v0.3e #59
};

// v0.2e: main + 사용자 함수들.
struct Program {
    Function              main;
    std::vector<Function> functions;
};

std::u32string opcode_name(Opcode op);
std::u32string disassemble(const Chunk& chunk);
std::u32string disassemble(const Program& prog);

Program compile(const ir::Program& ir);

// v0.3b: Program ↔ binary. .담음 의 코드 chunk 안으로 들어감.
// 비트 동일 round-trip 보장. little-endian, 모든 길이 u32 LE 접두.
std::vector<std::uint8_t> serialize(const Program& prog);
Program                   deserialize(const std::vector<std::uint8_t>& bytes);

}  // namespace seum::bytecode
