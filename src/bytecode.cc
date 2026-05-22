#include "bytecode.h"
#include "ir.h"
#include "utf8.h"

#include <sstream>
#include <stdexcept>

namespace seum::bytecode {

namespace {

struct Row {
    Opcode             op;
    const char32_t*    name;
    std::uint32_t      operand_bytes;  // 0, 1, or 4
};

const Row TABLE[] = {
    { Opcode::CONST_INT,    U"정수상수",     4 },
    { Opcode::CONST_STR,    U"문자열상수",   4 },
    { Opcode::PUSH_TRUE,    U"참",           0 },
    { Opcode::PUSH_FALSE,   U"거짓",         0 },
    { Opcode::PUSH_NIL,     U"없음",         0 },
    { Opcode::LOAD_GLOBAL,  U"전역가져오기", 4 },
    { Opcode::STORE_GLOBAL, U"전역두기",     4 },
    { Opcode::LOAD_LOCAL,   U"지역가져오기", 4 },
    { Opcode::STORE_LOCAL,  U"지역두기",     4 },
    { Opcode::CALL,         U"부르기",       1 },
    { Opcode::RET,          U"돌려주기",     0 },
    { Opcode::DROP,         U"버리기",       0 },
    { Opcode::DUP,          U"복사",         0 },
    { Opcode::IMPORT,       U"가져오기",     4 },
    { Opcode::NOT,          U"아닌",         0 },
    { Opcode::EQ,           U"같음",         0 },
    { Opcode::NE,           U"다름",         0 },
    { Opcode::LT,           U"작음",         0 },
    { Opcode::GT,           U"큼",           0 },
    { Opcode::LE,           U"작거나같음",   0 },
    { Opcode::GE,           U"크거나같음",   0 },
    { Opcode::JMP,          U"뛰기",         4 },
    { Opcode::JFZ,          U"거짓이면뛰기", 4 },
    { Opcode::NEG,          U"음수",         0 },
    { Opcode::ADD,          U"더하기",       0 },
    { Opcode::SUB,          U"빼기",         0 },
    { Opcode::MUL,          U"곱하기",       0 },
    { Opcode::DIV,          U"나누기",       0 },
    { Opcode::MOD,          U"나머지",       0 },
    { Opcode::HALT,         U"종료",         0 },
};

const Row* find_row(Opcode op) {
    for (const Row& r : TABLE) if (r.op == op) return &r;
    return nullptr;
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32_le_at(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    out[at    ] = static_cast<std::uint8_t>(v & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[at + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[at + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::u32string hex4(std::uint32_t v) {
    static const char32_t HEX[] = U"0123456789ABCDEF";
    std::u32string out;
    out += U"0x";
    for (int s = 12; s >= 0; s -= 4) out += HEX[(v >> s) & 0xF];
    return out;
}

std::u32string int_to_u32(std::int64_t v) {
    std::string s = std::to_string(v);
    return std::u32string(s.begin(), s.end());
}

std::u32string quote_string(const std::u32string& s) {
    std::u32string out;
    out += U"\""; out += s; out += U"\"";
    return out;
}

std::u32string pad_to(std::u32string s, std::size_t width) {
    if (s.size() < width) s.append(width - s.size(), U' ');
    return s;
}

template <typename T>
std::uint32_t intern(std::vector<T>& pool, const T& v) {
    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (pool[i] == v) return static_cast<std::uint32_t>(i);
    }
    pool.push_back(v);
    return static_cast<std::uint32_t>(pool.size() - 1);
}

struct ChunkCompiler {
    Chunk chunk;
    std::vector<std::uint32_t> ir_to_offset;
    std::vector<std::pair<std::uint32_t, ir::Label>> pending_patches;

    void emit_op(Opcode op, Position pos) {
        chunk.bytes.push_back(static_cast<std::uint8_t>(op));
        chunk.positions.push_back(pos);
    }
    void emit_u32(std::uint32_t v) {
        write_u32_le(chunk.bytes, v);
        chunk.positions.push_back({}); chunk.positions.push_back({});
        chunk.positions.push_back({}); chunk.positions.push_back({});
    }
    void emit_u8(std::uint8_t v) {
        chunk.bytes.push_back(v);
        chunk.positions.push_back({});
    }

    std::uint32_t intern_int (std::int64_t v)            { return intern(chunk.int_pool , v); }
    std::uint32_t intern_str (const std::u32string& v)   { return intern(chunk.str_pool , v); }
    std::uint32_t intern_name(const std::u32string& v)   { return intern(chunk.name_pool, v); }

    Opcode cmp_opcode(ir::Op op) {
        using O = ir::Op;
        switch (op) {
            case O::EQ: return Opcode::EQ;
            case O::NE: return Opcode::NE;
            case O::LT: return Opcode::LT;
            case O::GT: return Opcode::GT;
            case O::LE: return Opcode::LE;
            case O::GE: return Opcode::GE;
            case O::ADD: return Opcode::ADD;
            case O::SUB: return Opcode::SUB;
            case O::MUL: return Opcode::MUL;
            case O::DIV: return Opcode::DIV;
            case O::MOD: return Opcode::MOD;
            default:    return Opcode::HALT;
        }
    }

    void lower_instr(const ir::Instr& I) {
        using O = ir::Op;
        switch (I.op) {
            case O::CONST_INT:
                emit_op(Opcode::CONST_INT, I.pos);
                emit_u32(intern_int(I.int_val));
                break;
            case O::CONST_STR:
                emit_op(Opcode::CONST_STR, I.pos);
                emit_u32(intern_str(I.str_val));
                break;
            case O::PUSH_TRUE:  emit_op(Opcode::PUSH_TRUE , I.pos); break;
            case O::PUSH_FALSE: emit_op(Opcode::PUSH_FALSE, I.pos); break;
            case O::PUSH_NIL:   emit_op(Opcode::PUSH_NIL  , I.pos); break;
            case O::LOAD_GLOBAL:
                emit_op(Opcode::LOAD_GLOBAL, I.pos);
                emit_u32(intern_name(I.name));
                break;
            case O::STORE_GLOBAL:
                emit_op(Opcode::STORE_GLOBAL, I.pos);
                emit_u32(intern_name(I.name));
                break;
            case O::LOAD_LOCAL:
                emit_op(Opcode::LOAD_LOCAL, I.pos);
                emit_u32(I.local_idx);
                break;
            case O::STORE_LOCAL:
                emit_op(Opcode::STORE_LOCAL, I.pos);
                emit_u32(I.local_idx);
                break;
            case O::CALL:
                emit_op(Opcode::CALL, I.pos);
                if (I.arg_srcs.size() > 255) throw std::runtime_error("argc > 255");
                emit_u8(static_cast<std::uint8_t>(I.arg_srcs.size()));
                break;
            case O::DROP: emit_op(Opcode::DROP, I.pos); break;
            case O::DUP:  emit_op(Opcode::DUP,  I.pos); break;
            case O::IMPORT:
                emit_op(Opcode::IMPORT, I.pos);
                emit_u32(intern_name(I.name));
                break;
            case O::NOT: emit_op(Opcode::NOT, I.pos); break;
            case O::NEG: emit_op(Opcode::NEG, I.pos); break;
            case O::EQ: case O::NE: case O::LT:
            case O::GT: case O::LE: case O::GE:
            case O::ADD: case O::SUB: case O::MUL:
            case O::DIV: case O::MOD:
                emit_op(cmp_opcode(I.op), I.pos);
                break;
            case O::JMP:
            case O::JFZ: {
                Opcode op = (I.op == O::JMP) ? Opcode::JMP : Opcode::JFZ;
                emit_op(op, I.pos);
                std::uint32_t patch_at = static_cast<std::uint32_t>(chunk.bytes.size());
                emit_u32(0);
                pending_patches.emplace_back(patch_at, I.target);
                break;
            }
            case O::RET: emit_op(Opcode::RET, I.pos); break;
        }
    }
};

Chunk compile_function_chunk(const ir::Function& fn, bool is_main) {
    ChunkCompiler c;
    c.ir_to_offset.reserve(fn.instrs.size() + 1);
    for (const ir::Instr& I : fn.instrs) {
        c.ir_to_offset.push_back(static_cast<std::uint32_t>(c.chunk.bytes.size()));
        c.lower_instr(I);
    }
    c.ir_to_offset.push_back(static_cast<std::uint32_t>(c.chunk.bytes.size()));

    Position end_pos = fn.instrs.empty() ? Position{} : fn.instrs.back().pos;
    if (is_main) {
        c.emit_op(Opcode::HALT, end_pos);
    }
    // 함수 chunk 끝의 implicit RET 은 IR 단계에서 이미 추가됨.

    // 점프 패치
    for (auto [patch_at, target_label] : c.pending_patches) {
        if (target_label >= c.ir_to_offset.size()) {
            throw std::runtime_error("IR 점프 타깃이 범위를 벗어남");
        }
        write_u32_le_at(c.chunk.bytes, patch_at, c.ir_to_offset[target_label]);
    }
    return std::move(c.chunk);
}

}  // namespace

std::u32string opcode_name(Opcode op) {
    if (auto r = find_row(op)) return r->name;
    return U"???";
}

std::u32string disassemble(const Chunk& chunk) {
    std::u32string out;
    std::size_t offset = 0;
    while (offset < chunk.bytes.size()) {
        auto op = static_cast<Opcode>(chunk.bytes[offset]);
        const Row* row = find_row(op);
        out += U"  "; out += hex4(static_cast<std::uint32_t>(offset)); out += U"  ";
        out += pad_to(row ? std::u32string(row->name) : U"???", 14);
        if (!row) { out += U"\n"; ++offset; continue; }
        if (row->operand_bytes == 4) {
            std::uint32_t arg = read_u32_le(&chunk.bytes[offset + 1]);
            switch (op) {
                case Opcode::CONST_INT:    out += int_to_u32(chunk.int_pool[arg]); break;
                case Opcode::CONST_STR:    out += quote_string(chunk.str_pool[arg]); break;
                case Opcode::LOAD_GLOBAL:
                case Opcode::STORE_GLOBAL:
                case Opcode::IMPORT:       out += quote_string(chunk.name_pool[arg]); break;
                case Opcode::JMP:
                case Opcode::JFZ:          out += U"-> "; out += hex4(arg); break;
                default:                   out += int_to_u32(static_cast<std::int64_t>(arg)); break;
            }
        } else if (row->operand_bytes == 1) {
            std::uint32_t arg = chunk.bytes[offset + 1];
            out += int_to_u32(static_cast<std::int64_t>(arg));
        }
        out += U"\n";
        offset += 1 + row->operand_bytes;
    }
    return out;
}

std::u32string disassemble(const Program& prog) {
    std::u32string out;
    out += U"=== main ===\n";
    out += disassemble(prog.main.chunk);
    for (const Function& fn : prog.functions) {
        out += U"=== 함수 "; out += fn.name; out += U" ===\n";
        out += disassemble(fn.chunk);
    }
    return out;
}

Program compile(const ir::Program& ir) {
    Program prog;
    prog.main.name        = ir.main.name;
    prog.main.params      = ir.main.params;
    prog.main.local_count = static_cast<std::uint32_t>(ir.main.local_names.size());
    prog.main.chunk       = compile_function_chunk(ir.main, /*is_main=*/true);

    prog.functions.reserve(ir.functions.size());
    for (const ir::Function& fn : ir.functions) {
        Function bf;
        bf.name        = fn.name;
        bf.params      = fn.params;
        bf.local_count = static_cast<std::uint32_t>(fn.local_names.size());
        bf.is_getter   = fn.is_getter;            // v0.3e #59
        bf.chunk       = compile_function_chunk(fn, /*is_main=*/false);
        prog.functions.push_back(std::move(bf));
    }
    return prog;
}

// ===== v0.3b: Program ↔ bytes 직렬화 =====
namespace {

void put_u32_io(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}
void put_u64_io(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}
void put_str_io(std::vector<std::uint8_t>& out, const std::u32string& s) {
    std::string u8 = utf8::encode(s);
    put_u32_io(out, static_cast<std::uint32_t>(u8.size()));
    for (char c : u8) out.push_back(static_cast<std::uint8_t>(c));
}

void put_chunk_io(std::vector<std::uint8_t>& out, const Chunk& c) {
    put_u32_io(out, static_cast<std::uint32_t>(c.bytes.size()));
    out.insert(out.end(), c.bytes.begin(), c.bytes.end());

    put_u32_io(out, static_cast<std::uint32_t>(c.int_pool.size()));
    for (std::int64_t v : c.int_pool) put_u64_io(out, static_cast<std::uint64_t>(v));

    put_u32_io(out, static_cast<std::uint32_t>(c.str_pool.size()));
    for (const std::u32string& s : c.str_pool) put_str_io(out, s);

    put_u32_io(out, static_cast<std::uint32_t>(c.name_pool.size()));
    for (const std::u32string& s : c.name_pool) put_str_io(out, s);

    put_u32_io(out, static_cast<std::uint32_t>(c.positions.size()));
    for (const Position& p : c.positions) {
        put_u32_io(out, static_cast<std::uint32_t>(p.line));
        put_u32_io(out, static_cast<std::uint32_t>(p.column));
    }
}

void put_function_io(std::vector<std::uint8_t>& out, const Function& fn) {
    put_str_io(out, fn.name);
    put_u32_io(out, static_cast<std::uint32_t>(fn.params.size()));
    for (const std::u32string& p : fn.params) put_str_io(out, p);
    put_u32_io(out, fn.local_count);
    put_u32_io(out, fn.is_getter ? 1u : 0u);   // v0.3e #59
    put_chunk_io(out, fn.chunk);
}

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;
    void need(std::size_t n) {
        if (p + n > end) throw std::runtime_error("bytecode::deserialize 끝 초과");
    }
    std::uint32_t get_u32() {
        need(4);
        std::uint32_t v = static_cast<std::uint32_t>(p[0])
                        | (static_cast<std::uint32_t>(p[1]) << 8)
                        | (static_cast<std::uint32_t>(p[2]) << 16)
                        | (static_cast<std::uint32_t>(p[3]) << 24);
        p += 4;
        return v;
    }
    std::uint64_t get_u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
        p += 8;
        return v;
    }
    std::u32string get_str() {
        std::uint32_t len = get_u32();
        need(len);
        std::string u8(reinterpret_cast<const char*>(p), len);
        p += len;
        return utf8::decode(u8);
    }
    Chunk get_chunk() {
        Chunk c;
        std::uint32_t bs = get_u32();
        need(bs);
        c.bytes.assign(p, p + bs); p += bs;

        std::uint32_t ic = get_u32();
        c.int_pool.reserve(ic);
        for (std::uint32_t i = 0; i < ic; ++i) c.int_pool.push_back(static_cast<std::int64_t>(get_u64()));

        std::uint32_t sc = get_u32();
        c.str_pool.reserve(sc);
        for (std::uint32_t i = 0; i < sc; ++i) c.str_pool.push_back(get_str());

        std::uint32_t nc = get_u32();
        c.name_pool.reserve(nc);
        for (std::uint32_t i = 0; i < nc; ++i) c.name_pool.push_back(get_str());

        std::uint32_t pc = get_u32();
        c.positions.reserve(pc);
        for (std::uint32_t i = 0; i < pc; ++i) {
            Position pos;
            pos.line   = get_u32();
            pos.column = get_u32();
            c.positions.push_back(pos);
        }
        return c;
    }
    Function get_function() {
        Function fn;
        fn.name = get_str();
        std::uint32_t pc = get_u32();
        fn.params.reserve(pc);
        for (std::uint32_t i = 0; i < pc; ++i) fn.params.push_back(get_str());
        fn.local_count = get_u32();
        fn.is_getter   = (get_u32() != 0);   // v0.3e #59
        fn.chunk = get_chunk();
        return fn;
    }
};

}  // namespace

std::vector<std::uint8_t> serialize(const Program& prog) {
    std::vector<std::uint8_t> out;
    put_function_io(out, prog.main);
    put_u32_io(out, static_cast<std::uint32_t>(prog.functions.size()));
    for (const Function& fn : prog.functions) put_function_io(out, fn);
    return out;
}

Program deserialize(const std::vector<std::uint8_t>& bytes) {
    Reader r{bytes.data(), bytes.data() + bytes.size()};
    Program prog;
    prog.main = r.get_function();
    std::uint32_t fc = r.get_u32();
    prog.functions.reserve(fc);
    for (std::uint32_t i = 0; i < fc; ++i) prog.functions.push_back(r.get_function());
    return prog;
}

}  // namespace seum::bytecode
