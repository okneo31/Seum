#include <catch2/catch_amalgamated.hpp>

#include "bytecode.h"
#include "dameum.h"
#include "environment.h"
#include "error_reporter.h"
#include "interpreter.h"
#include "ir.h"
#include "jamo_huffman.h"
#include "lexer.h"
#include "modules/builtin.h"
#include "modules/finance.h"
#include "modules/native_bridge.h"
#include "modules/time.h"
#include "parser.h"
#include "utf8.h"
#include "vm.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace seum;

namespace {
std::string acc_temp_path(const char* name_utf8) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / fs::u8path(name_utf8);
    return p.u8string();
}
}  // namespace

// 두 실행 경로(트리워킹·VM)가 동일 소스에 대해 동일한 sink 출력을 내야 한다.
// 잠긴 결정 #21 + #32 (양 경로 동일 언어 의무).
struct Both {
    std::u32string tree;
    std::u32string vm_;
};

static Both run_both(std::u32string_view src) {
    Both out;

    {
        auto sink = [&out](const std::u32string& s) { out.tree += s; };
        Environment env;
        modules::register_builtin(env, sink);
        modules::register_time(env);
        modules::register_finance(env);
        auto prog = parse(tokenize(src));
        evaluate(prog, env);
    }
    {
        auto sink = [&out](const std::u32string& s) { out.vm_ += s; };
        Environment env;
        modules::register_builtin(env, sink);
        modules::register_time(env);
        modules::register_finance(env);
        auto prog    = parse(tokenize(src));
        auto ir_prog = ir::lower(prog);
        auto bc_prog = bytecode::compile(ir_prog);
        vm::run(bc_prog, env);
    }
    return out;
}

TEST_CASE("양경로 회귀: MVP 슬라이스 — 가져오기·변수·시각 출력 파이프라인", "[integration]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_builtin(env, sink);
    modules::register_time(env);

    auto toks = tokenize(U"가져오기(시간).\n변수 t = 지금.\n보여주기(t).");
    auto prog = parse(toks);

    REQUIRE_NOTHROW(evaluate(prog, env));
    REQUIRE(captured.find(U"세움력") != std::u32string::npos);
    REQUIRE(captured.find(U"SST")    != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 정적 문자열 출력 (acceptance 2)", "[integration][both]") {
    auto b = run_both(U"변수 안녕 = \"세움\".\n보여주기(안녕).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"세움") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 가져오기 + 보여주기(지금) (acceptance 1)", "[integration][both]") {
    auto b = run_both(U"가져오기(시간).\n보여주기(지금).");
    // 시각은 1초 차이로 달라질 수 있으므로 형식만 비교: 같은 형식이어야 함.
    REQUIRE(b.tree.find(U"세움력") != std::u32string::npos);
    REQUIRE(b.vm_.find(U"세움력")  != std::u32string::npos);
    REQUIRE(b.tree.find(U"SST")   != std::u32string::npos);
    REQUIRE(b.vm_.find(U"SST")    != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 함수 호출 인자에 변수 (annyeong.세움)", "[integration][both]") {
    auto b = run_both(U"변수 인사 = \"안녕\".\n보여주기(인사).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"안녕") != std::u32string::npos);
}

// === Acceptance 5 (v0.2b) ===
TEST_CASE("양경로 acceptance 5: 만약 (1 == 1) 이면 같음 아니면 다름", "[integration][both][v02b]") {
    auto b = run_both(U"만약 (1 == 1) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"같음") != std::u32string::npos);
    REQUIRE(b.tree.find(U"다름") == std::u32string::npos);
}

TEST_CASE("양경로 acceptance 5 거짓 분기", "[integration][both][v02b]") {
    auto b = run_both(U"만약 (1 == 2) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"다름") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 비교 + 논리 연산자", "[integration][both][v02b]") {
    auto b = run_both(U"보여주기((1 < 5) && (3 == 3)). 보여주기(!(1 == 2)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"참") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: else-if 중첩", "[integration][both][v02b]") {
    auto b = run_both(
        U"변수 n = 2.\n"
        U"만약 (n == 1) 이면 { 보여주기(\"하나\"). } 아니면 만약 (n == 2) 이면 { 보여주기(\"둘\"). } 아니면 { 보여주기(\"기타\"). }");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"둘") != std::u32string::npos);
    REQUIRE(b.tree.find(U"하나") == std::u32string::npos);
    REQUIRE(b.tree.find(U"기타") == std::u32string::npos);
}

// === Acceptance 6/7/8 (v0.2c) ===
TEST_CASE("양경로 acceptance 6: 1 + 2 * 3 = 7 (우선순위)", "[integration][both][v02c]") {
    auto b = run_both(U"보여주기(1 + 2 * 3).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"7") != std::u32string::npos);
}

TEST_CASE("양경로 acceptance 7: 문자열 결합", "[integration][both][v02c]") {
    auto b = run_both(U"보여주기(\"세움\" + \" 안녕\").");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"세움 안녕") != std::u32string::npos);
}

TEST_CASE("양경로 acceptance 8: 0 나눗셈은 한국어 에러", "[integration][both][v02c]") {
    // tree 경로
    {
        std::u32string out;
        auto sink = [&out](const std::u32string& s) { out += s; };
        Environment env;
        modules::register_builtin(env, sink);
        auto prog = parse(tokenize(U"보여주기(1 / 0)."));
        try {
            evaluate(prog, env);
            FAIL("tree 경로에서 예외가 던져졌어야 합니다");
        } catch (const SeumError& e) {
            std::string msg = e.what();
            REQUIRE(msg.find("0으로 나눌 수 없습니다") != std::string::npos);
        }
    }
    // vm 경로
    {
        std::u32string out;
        auto sink = [&out](const std::u32string& s) { out += s; };
        Environment env;
        modules::register_builtin(env, sink);
        auto prog    = parse(tokenize(U"보여주기(1 / 0)."));
        auto ir_prog = ir::lower(prog);
        auto bc_prog = bytecode::compile(ir_prog);
        try {
            vm::run(bc_prog, env);
            FAIL("vm 경로에서 예외가 던져졌어야 합니다");
        } catch (const SeumError& e) {
            std::string msg = e.what();
            REQUIRE(msg.find("0으로 나눌 수 없습니다") != std::string::npos);
        }
    }
}

TEST_CASE("양경로 회귀: 단항 - 와 산술 조합", "[integration][both][v02c]") {
    auto b = run_both(U"보여주기(-3 + 5 * 2). 보여주기((10 - 4) / 2).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"7") != std::u32string::npos);
    REQUIRE(b.tree.find(U"3") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 문자열+정수 = 타입 에러", "[integration][both][v02c]") {
    Environment env;
    modules::register_builtin(env);
    auto prog = parse(tokenize(U"보여주기(\"세움\" + 1)."));
    REQUIRE_THROWS_AS(evaluate(prog, env), SeumError);
}

// === Acceptance 9/10 (v0.2d) ===
TEST_CASE("양경로 acceptance 9: 반복 3번 야!", "[integration][both][v02d]") {
    auto b = run_both(U"반복 3번 { 보여주기(\"야!\"). }");
    REQUIRE(b.tree == b.vm_);
    // "야!" 3 번
    auto count_hits = [](const std::u32string& s, std::u32string_view needle) {
        std::size_t n = 0, at = 0;
        while ((at = s.find(needle, at)) != std::u32string::npos) { ++n; at += needle.size(); }
        return n;
    };
    REQUIRE(count_hits(b.tree, U"야!") == 3);
}

TEST_CASE("양경로 acceptance 10: 동안 카운터", "[integration][both][v02d]") {
    auto b = run_both(U"변수 i = 0. 동안 (i < 3) { 보여주기(i). 변수 i = i + 1. }");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"0") != std::u32string::npos);
    REQUIRE(b.tree.find(U"1") != std::u32string::npos);
    REQUIRE(b.tree.find(U"2") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 반복 안에 만약 중첩 — 한 번만 hit", "[integration][both][v02d]") {
    auto b = run_both(
        U"변수 i = 0. 반복 5번 { 만약 (i == 2) 이면 { 보여주기(\"hit\"). } 변수 i = i + 1. }");
    REQUIRE(b.tree == b.vm_);
    auto count_hits = [](const std::u32string& s, std::u32string_view needle) {
        std::size_t n = 0, at = 0;
        while ((at = s.find(needle, at)) != std::u32string::npos) { ++n; at += needle.size(); }
        return n;
    };
    REQUIRE(count_hits(b.tree, U"hit") == 1);
}

TEST_CASE("양경로 회귀: 반복 0번 = 0 회", "[integration][both][v02d]") {
    auto b = run_both(U"반복 0번 { 보여주기(\"X\"). } 보여주기(\"끝\").");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"X") == std::u32string::npos);
    REQUIRE(b.tree.find(U"끝") != std::u32string::npos);
}

// === Acceptance 11/12 (v0.2e) ===

TEST_CASE("양경로 acceptance 11: 함수 더하기(a,b) = 7", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 더하기(a, b) -> 합 { 돌려주기 a + b. }\n"
        U"보여주기(더하기(3, 4)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"7") != std::u32string::npos);
}

TEST_CASE("양경로 acceptance 12: 재귀 팩토리얼 5! = 120", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 팩토리얼(n) -> 결과 {\n"
        U"  만약 (n <= 1) 이면 { 돌려주기 1. }\n"
        U"  돌려주기 n * 팩토리얼(n - 1).\n"
        U"}\n"
        U"보여주기(팩토리얼(5)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"120") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 함수 안 지역 변수 + 반복", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 합계(n) -> 결과 {\n"
        U"  변수 합 = 0. 변수 i = 1.\n"
        U"  동안 (i <= n) { 변수 합 = 합 + i. 변수 i = i + 1. }\n"
        U"  돌려주기 합.\n"
        U"}\n"
        U"보여주기(합계(10)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"55") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 재귀 피보 fib(10) = 55", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 피보(n) -> 결과 {\n"
        U"  만약 (n < 2) 이면 { 돌려주기 n. }\n"
        U"  돌려주기 피보(n - 1) + 피보(n - 2).\n"
        U"}\n"
        U"보여주기(피보(10)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"55") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 함수가 외부 글로벌 함수 호출", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 두배(x) -> 결과 { 돌려주기 x * 2. }\n"
        U"함수 네배(x) -> 결과 { 돌려주기 두배(두배(x)). }\n"
        U"보여주기(네배(3)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"12") != std::u32string::npos);
}

TEST_CASE("양경로 회귀: 돌려주기 없이 끝나는 함수 = 없음", "[integration][both][v02e]") {
    auto b = run_both(
        U"함수 비어() -> 없음 { }\n"
        U"보여주기(비어()).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"없음") != std::u32string::npos);
}

// === Acceptance 14 (v0.3c): 한글텍스트 chunk round-trip 비트 동일 ===

TEST_CASE("Acceptance 14: 한글텍스트 chunk .담음 round-trip 비트 동일", "[integration][v03c]") {
    std::u32string orig = U"세움세상의 평화를 위한 그릇 — 한국어 콘텐츠의 정확한 표현·압축";

    // import 흉내: orig → jamo::compress → KoreanText chunk
    dameum::Container in;
    dameum::Chunk ch;
    ch.type = dameum::ChunkType::KoreanText;
    ch.compression = dameum::Compression::JamoHuffman;
    ch.data = jamo::compress(orig);
    ch.uncompressed_size = static_cast<std::uint32_t>(orig.size() * 4);  // UTF-32 raw
    in.chunks.push_back(std::move(ch));

    auto path = acc_temp_path("acc14_korean.담음");
    dameum::write(in, path);

    // export 흉내: read → 동일 chunk 확인 → decompress → orig 비트 동일
    dameum::Container out = dameum::read(path);
    REQUIRE(out.chunks.size() == 1);
    REQUIRE(out.chunks[0].type == dameum::ChunkType::KoreanText);
    REQUIRE(out.chunks[0].compression == dameum::Compression::JamoHuffman);
    REQUIRE(out.chunks[0].data == in.chunks[0].data);  // chunk binary 비트 동일

    auto decoded = jamo::decompress(out.chunks[0].data);
    REQUIRE(decoded == orig);  // 텍스트 round-trip 비트 동일 (F11 측정 기준)
}

TEST_CASE("Acceptance 14: 빈 한글텍스트 chunk round-trip", "[integration][v03c]") {
    std::u32string orig = U"";
    dameum::Container in;
    dameum::Chunk ch;
    ch.type = dameum::ChunkType::KoreanText;
    ch.compression = dameum::Compression::JamoHuffman;
    ch.data = jamo::compress(orig);
    ch.uncompressed_size = 0;
    in.chunks.push_back(std::move(ch));

    auto path = acc_temp_path("acc14_empty.담음");
    dameum::write(in, path);
    auto out = dameum::read(path);
    REQUIRE(jamo::decompress(out.chunks[0].data) == orig);
}

// === Acceptance 15 (v0.3d): .txt → .담음 → .txt 비트 동일 ===

namespace {

void write_bytes(const std::string& path, const std::string& bytes) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

// import flow (main.cc 의 convert_file 와 동일 알고리즘 — round-trip 만 확인하는 단위 테스트)
void txt_to_dameum(const std::string& txt_path, const std::string& dameum_path) {
    std::string raw_utf8 = read_bytes(txt_path);
    std::u32string text  = utf8::decode(raw_utf8);
    dameum::Container c;
    dameum::Chunk ch;
    ch.type              = dameum::ChunkType::KoreanText;
    ch.compression       = dameum::Compression::JamoHuffman;
    ch.data              = jamo::compress(text);
    ch.uncompressed_size = static_cast<std::uint32_t>(raw_utf8.size());
    c.chunks.push_back(std::move(ch));
    dameum::write(c, dameum_path);
}

void dameum_to_txt(const std::string& dameum_path, const std::string& txt_path) {
    dameum::Container c = dameum::read(dameum_path);
    std::u32string text;
    for (const dameum::Chunk& ch : c.chunks) {
        if (ch.type == dameum::ChunkType::KoreanText) {
            text = jamo::decompress(ch.data);
            break;
        }
    }
    write_bytes(txt_path, utf8::encode(text));
}

}  // namespace

TEST_CASE("Acceptance 15: .txt → .담음 → .txt 비트 동일 (한국어)", "[integration][v03d]") {
    std::string orig =
        "\xEC\x84\xB8\xEC\x9B\x80\xEC\x84\xB8\xEC\x83\x81\xEC\x9D\x98 "  // 세움세상의 (UTF-8)
        "\xED\x8F\x89\xED\x99\x94\xEB\xA5\xBC \xEC\x9C\x84\xED\x95\x9C " // 평화를 위한
        "\xEA\xB7\xB8\xEB\xA6\x87\n"                                     // 그릇\n
        "v0.3d round-trip test\n";

    auto txt_orig  = acc_temp_path("acc15_orig.txt");
    auto mid_dam   = acc_temp_path("acc15_mid.담음");
    auto txt_back  = acc_temp_path("acc15_back.txt");
    write_bytes(txt_orig, orig);

    txt_to_dameum(txt_orig, mid_dam);
    dameum_to_txt(mid_dam, txt_back);

    REQUIRE(read_bytes(txt_back) == orig);
}

TEST_CASE("Acceptance 15: 빈 .txt round-trip", "[integration][v03d]") {
    auto txt_orig = acc_temp_path("acc15_empty.txt");
    auto mid_dam  = acc_temp_path("acc15_empty.담음");
    auto txt_back = acc_temp_path("acc15_empty_back.txt");
    write_bytes(txt_orig, "");
    txt_to_dameum(txt_orig, mid_dam);
    dameum_to_txt(mid_dam, txt_back);
    REQUIRE(read_bytes(txt_back) == "");
}

TEST_CASE("Acceptance 15: ASCII-only .txt round-trip", "[integration][v03d]") {
    std::string orig = "Hello, Seum 0.3d!\nLine 2.\n";
    auto txt_orig = acc_temp_path("acc15_ascii.txt");
    auto mid_dam  = acc_temp_path("acc15_ascii.담음");
    auto txt_back = acc_temp_path("acc15_ascii_back.txt");
    write_bytes(txt_orig, orig);
    txt_to_dameum(txt_orig, mid_dam);
    dameum_to_txt(mid_dam, txt_back);
    REQUIRE(read_bytes(txt_back) == orig);
}

// === Acceptance 16/17 (v0.3e): 그릇 ≡ 담음 자기-검증 ===

namespace {

// 헬퍼: .세움 source → .담음 (in-process build).
void build_grut_to_dameum(std::u32string_view source, const std::string& dameum_path) {
    auto bc = bytecode::compile(ir::lower(parse(tokenize(source))));
    dameum::Container c;
    dameum::Chunk code;
    code.type              = dameum::ChunkType::Code;
    code.compression       = dameum::Compression::Raw;
    code.data              = bytecode::serialize(bc);
    code.uncompressed_size = static_cast<std::uint32_t>(code.data.size());
    c.chunks.push_back(std::move(code));
    dameum::write(c, dameum_path);
}

// 헬퍼: temp lib_dir 안에 그릇 .담음 빌드.
std::string build_grut_in_lib(const std::string& name, std::u32string_view source) {
    namespace fs = std::filesystem;
    fs::path lib_dir = fs::temp_directory_path() / "seum_v03e_lib";
    fs::create_directories(lib_dir);
    fs::path out = lib_dir / fs::u8path(name + ".\xEB\x8B\xB4\xEC\x9D\x8C");  // <name>.담음
    build_grut_to_dameum(source, out.u8string());
    return lib_dir.u8string();
}

// 헬퍼: resolver — lib_dir 안에서 .담음 찾아 register_program.
std::vector<std::shared_ptr<bytecode::Program>> g_test_progs;

bool test_resolver(const std::u32string& name, Environment& env, const std::string& lib_dir) {
    namespace fs = std::filesystem;
    std::string utf8_name = utf8::encode(name);
    std::string fname = utf8_name + ".\xEB\x8B\xB4\xEC\x9D\x8C";
    fs::path path = fs::u8path(lib_dir) / fs::u8path(fname);
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;
    auto c = dameum::read(path.u8string());
    for (const auto& ch : c.chunks) {
        if (ch.type == dameum::ChunkType::Code) {
            auto prog = std::make_shared<bytecode::Program>(bytecode::deserialize(ch.data));
            g_test_progs.push_back(prog);
            vm::register_program(*prog, env);
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Acceptance 16: 사용자 정의 그릇 .세움 → .담음 → 가져오기", "[integration][v03e]") {
    auto lib_dir = build_grut_in_lib(
        "안녕",
        U"함수 인사() -> 결과 { 돌려주기 \"안녕하세요\". }");

    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_builtin(env, sink);   // 보여주기 builtin (자기-검증 narrow)
    modules::register_natives(env, sink);
    env.set_dameum_resolver([lib_dir](const std::u32string& name, Environment& e) -> bool {
        return test_resolver(name, e, lib_dir);
    });

    // 사용자 그릇을 .담음 으로 import, 그 안의 함수 호출.
    auto src = U"가져오기(안녕). 보여주기(인사()).";
    auto bc  = bytecode::compile(ir::lower(parse(tokenize(src))));
    REQUIRE_NOTHROW(vm::run(bc, env));
    REQUIRE(captured.find(U"안녕하세요") != std::u32string::npos);
}

TEST_CASE("Acceptance 16: 지연값을 .담음 안에 보존", "[integration][v03e]") {
    auto lib_dir = build_grut_in_lib(
        "상수",
        U"지연값 사십이() -> 결과 { 돌려주기 42. }");

    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_builtin(env, sink);
    modules::register_natives(env, sink);
    env.set_dameum_resolver([lib_dir](const std::u32string& name, Environment& e) -> bool {
        return test_resolver(name, e, lib_dir);
    });

    auto src = U"가져오기(상수). 보여주기(사십이).";  // 지연값 = 자동 발화
    auto bc  = bytecode::compile(ir::lower(parse(tokenize(src))));
    REQUIRE_NOTHROW(vm::run(bc, env));
    REQUIRE(captured.find(U"42") != std::u32string::npos);
}

TEST_CASE("Acceptance 17: builtin 보여주기 그릇 .담음 자기-검증", "[integration][v03e]") {
    // 보여주기 그릇 .세움 = 위험 { native_보여주기(값) } wrapper.
    auto lib_dir = build_grut_in_lib(
        "보여주기그릇",
        U"함수 보여주기2(값) -> 없음 { 위험 { native_보여주기(값) }. }");

    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_natives(env, sink);   // native_보여주기 만 (register_builtin 안 부름)
    env.set_dameum_resolver([lib_dir](const std::u32string& name, Environment& e) -> bool {
        return test_resolver(name, e, lib_dir);
    });

    auto src = U"가져오기(보여주기그릇). 보여주기2(\"세움\").";
    auto bc  = bytecode::compile(ir::lower(parse(tokenize(src))));
    REQUIRE_NOTHROW(vm::run(bc, env));
    REQUIRE(captured.find(U"세움") != std::u32string::npos);
}

TEST_CASE("Acceptance 17: 그릇 없으면 가져오기 한국어 에러", "[integration][v03e]") {
    Environment env;
    modules::register_builtin(env);
    modules::register_natives(env);
    env.set_dameum_resolver([](const std::u32string&, Environment&) -> bool { return false; });

    auto src = U"가져오기(없는그릇).";
    auto bc  = bytecode::compile(ir::lower(parse(tokenize(src))));
    REQUIRE_THROWS_AS(vm::run(bc, env), SeumError);
}

// === Acceptance 1-12 양 경로 회귀 (.담음 빌드 경로) — 명세 #52 의 ship 기준 (a) ===

namespace {

std::u32string run_via_dameum(std::u32string_view src) {
    // .세움 source → .담음 임시파일 → 로드 → vm 실행
    auto bc = bytecode::compile(ir::lower(parse(tokenize(src))));
    auto tmp = acc_temp_path("via_dam_run.담음");

    dameum::Container c;
    dameum::Chunk code;
    code.type              = dameum::ChunkType::Code;
    code.compression       = dameum::Compression::Raw;
    code.data              = bytecode::serialize(bc);
    code.uncompressed_size = static_cast<std::uint32_t>(code.data.size());
    c.chunks.push_back(std::move(code));
    dameum::write(c, tmp);

    auto c2 = dameum::read(tmp);
    bytecode::Program prog;
    for (const auto& ch : c2.chunks) {
        if (ch.type == dameum::ChunkType::Code) {
            prog = bytecode::deserialize(ch.data);
            break;
        }
    }

    std::u32string captured;
    Environment env;
    modules::register_builtin(env, [&captured](const std::u32string& s) { captured += s; });
    modules::register_time(env);
    modules::register_natives(env);
    modules::register_finance(env);
    vm::run(prog, env);
    return captured;
}

}  // namespace

TEST_CASE(".담음 빌드 경로: acc2 변수+보여주기", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"변수 안녕 = \"세움\". 보여주기(안녕).");
    REQUIRE(out.find(U"세움") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc1 시간 가져오기 + 지금", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"가져오기(시간). 보여주기(지금).");
    REQUIRE(out.find(U"세움력") != std::u32string::npos);
    REQUIRE(out.find(U"SST")   != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc5 만약/아니면", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(
        U"만약 (1 == 1) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }");
    REQUIRE(out.find(U"같음") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc6 산술 우선순위", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"보여주기(1 + 2 * 3).");
    REQUIRE(out.find(U"7") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc7 문자열 결합", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"보여주기(\"세움\" + \" 안녕\").");
    REQUIRE(out.find(U"세움 안녕") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc9 반복 3번", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"반복 3번 { 보여주기(\"!\"). }");
    std::size_t n = 0, at = 0;
    while ((at = out.find(U"!", at)) != std::u32string::npos) { ++n; ++at; }
    REQUIRE(n == 3);
}

TEST_CASE(".담음 빌드 경로: acc10 동안 카운터", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(U"변수 i = 0. 동안 (i < 3) { 보여주기(i). 변수 i = i + 1. }");
    REQUIRE(out.find(U"0") != std::u32string::npos);
    REQUIRE(out.find(U"1") != std::u32string::npos);
    REQUIRE(out.find(U"2") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc11 함수 더하기", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(
        U"함수 더하기(a, b) -> 합 { 돌려주기 a + b. } 보여주기(더하기(3, 4)).");
    REQUIRE(out.find(U"7") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: acc12 재귀 팩토리얼", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(
        U"함수 팩토리얼(n) -> 결과 {\n"
        U"  만약 (n <= 1) 이면 { 돌려주기 1. }\n"
        U"  돌려주기 n * 팩토리얼(n - 1).\n"
        U"}\n"
        U"보여주기(팩토리얼(5)).");
    REQUIRE(out.find(U"120") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 위험 식 + 지연값 보존", "[integration][v03e][acc-dameum]") {
    auto out = run_via_dameum(
        U"지연값 더블세븐() -> 결과 { 돌려주기 위험 { 77 }. }\n"
        U"보여주기(더블세븐).");
    REQUIRE(out.find(U"77") != std::u32string::npos);
}

TEST_CASE("Acceptance 14: 한글+ASCII 혼합 round-trip", "[integration][v03c]") {
    std::u32string orig = U"세움 v0.3c — Jamo Huffman 적용. 압축률 0.x 측정.";
    auto compressed = jamo::compress(orig);
    auto decoded    = jamo::decompress(compressed);
    REQUIRE(decoded == orig);

    dameum::Container in;
    dameum::Chunk ch;
    ch.type = dameum::ChunkType::KoreanText;
    ch.compression = dameum::Compression::JamoHuffman;
    ch.data = compressed;
    ch.uncompressed_size = static_cast<std::uint32_t>(orig.size() * 4);
    in.chunks.push_back(std::move(ch));

    auto path = acc_temp_path("acc14_mixed.담음");
    dameum::write(in, path);
    auto out = dameum::read(path);
    REQUIRE(jamo::decompress(out.chunks[0].data) == orig);
}

// === v0.4a-1 2b: 레코드·멤버 접근 양 경로 의무 (#32, 결정 69·79·93) ===

TEST_CASE("양경로 v0.4a-1: 레코드 멤버 읽기", "[integration][both][v04a]") {
    auto b = run_both(U"변수 위치 = (ㄱ: 10, ㅅ: 20). 보여주기(위치.ㄱ). 보여주기(위치.ㅅ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"10") != std::u32string::npos);
    REQUIRE(b.tree.find(U"20") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 중첩 레코드 체이닝", "[integration][both][v04a]") {
    auto b = run_both(U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 보여주기(용사.위치.ㄱ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"10") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 레코드 출력 형태 동일", "[integration][both][v04a]") {
    auto b = run_both(U"보여주기((ㄱ: 1, ㅅ: 2)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"(ㄱ: 1, ㅅ: 2)") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 레코드 필드 값으로 식 사용", "[integration][both][v04a]") {
    auto b = run_both(U"변수 r = (합: 1 + 2 * 3). 보여주기(r.합).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"7") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 레코드 멤버 접근", "[integration][v04a][acc-dameum]") {
    // 레코드/멤버가 bytecode 직렬화 → .담음 → 역직렬화 → 실행을 통과하는지
    auto out = run_via_dameum(U"변수 위치 = (ㄱ: 7, ㅅ: 8). 보여주기(위치.ㄱ).");
    REQUIRE(out.find(U"7") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 레코드를 함수 인자로 전달", "[integration][both][v04a]") {
    auto b = run_both(
        U"함수 가로(점) -> 결과 { 돌려주기 점.ㄱ. }\n"
        U"보여주기(가로((ㄱ: 5, ㅅ: 9))).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"5") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 레코드 필드에 문자열·불리언", "[integration][both][v04a]") {
    auto b = run_both(
        U"변수 용사 = (이름: \"용사\", 살아있음: 참).\n"
        U"보여주기(용사.이름). 보여주기(용사.살아있음).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"용사") != std::u32string::npos);
    REQUIRE(b.tree.find(U"참")   != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 3중 중첩 레코드 체이닝", "[integration][both][v04a]") {
    auto b = run_both(U"변수 깊이 = (ㄱ: (ㅅ: (ㄷ: 42))). 보여주기(깊이.ㄱ.ㅅ.ㄷ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"42") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 레코드 == 비교는 거짓 (결정 93d)", "[integration][both][v04a]") {
    auto b = run_both(U"보여주기((ㄱ: 1) == (ㄱ: 1)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"거짓") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-1: 다른 레코드의 멤버를 필드 값으로", "[integration][both][v04a]") {
    auto b = run_both(U"변수 가 = (x: 7). 변수 나 = (y: 가.x). 보여주기(나.y).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"7") != std::u32string::npos);
}

namespace {
// N개 필드 레코드 소스 생성 — 키 ㅋ0..ㅋ(N-1), 값 0..N-1 (모두 distinct).
std::u32string big_record_src(int n) {
    std::u32string s = U"변수 큰것 = (";
    for (int i = 0; i < n; ++i) {
        if (i) s += U", ";
        std::string num = std::to_string(i);
        s += U"ㅋ";
        for (char c : num) s += static_cast<char32_t>(c);
        s += U": ";
        for (char c : num) s += static_cast<char32_t>(c);
    }
    s += U").";
    return s;
}
}  // namespace

TEST_CASE("v0.4a-1 정리: 256-필드 레코드는 양 경로 모두 에러 (#32)", "[integration][v04a]") {
    auto src = big_record_src(256);
    {   // 트리 경로
        Environment env;
        modules::register_builtin(env);
        REQUIRE_THROWS_AS(evaluate(parse(tokenize(src)), env), SeumError);
    }
    {   // VM 경로 (bytecode::compile 단계에서 거부)
        Environment env;
        modules::register_builtin(env);
        auto prog = parse(tokenize(src));
        REQUIRE_THROWS_AS(vm::run(bytecode::compile(ir::lower(prog)), env), SeumError);
    }
}

TEST_CASE("v0.4a-1 정리: 255-필드 레코드는 양 경로 모두 정상", "[integration][both][v04a]") {
    auto src = big_record_src(255);
    src += U" 보여주기(큰것.ㅋ0).";
    auto b = run_both(src);
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"0") != std::u32string::npos);
}

// === v0.4a-2: 필드 대입 + 복합대입 양 경로 의무 (#32, 결정 81·93) ===

TEST_CASE("양경로 v0.4a-2: 복합대입 += 중첩 레코드", "[integration][both][v04a2]") {
    auto b = run_both(
        U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 용사.위치.ㄱ += 10. 보여주기(용사.위치.ㄱ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"20") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-2: 참조 의미론 — 별칭 변이 (결정 93)", "[integration][both][v04a2]") {
    auto b = run_both(U"변수 가 = (ㄱ: 1). 변수 나 = 가. 나.ㄱ = 99. 보여주기(가.ㄱ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"99") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-2: 원본 게임 예시 — 용사 이동", "[integration][both][v04a2]") {
    // 프로그래밍예시.md 핵심 — 용사.위치.ㄱ += 10 (콜백 없이 직접 두 번)
    auto b = run_both(
        U"변수 용사 = (모양: \"용사\", 위치: (ㄱ: 10, ㅅ: 20)).\n"
        U"용사.위치.ㄱ += 10.\n"
        U"용사.위치.ㄱ += 10.\n"
        U"보여주기(용사.위치.ㄱ).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"30") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 필드 대입", "[integration][v04a2][acc-dameum]") {
    auto out = run_via_dameum(U"변수 r = (값: 1). r.값 += 41. 보여주기(r.값).");
    REQUIRE(out.find(U"42") != std::u32string::npos);
}

// === v0.4a-3: 함수 1급 — 함수값·변수 바인딩 (결정 62) ===
// 함수는 CallableValue 로 환경에 등록되므로 기존 인프라로 1급 동작.

TEST_CASE("양경로 v0.4a-3: 함수를 변수에 바인딩", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"변수 f = 두배.\n"
        U"보여주기(f(7)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"14") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-3: 함수를 인자로 전달 (고차 함수)", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 적용(연산, 값) -> 결과 { 돌려주기 연산(값). }\n"
        U"보여주기(적용(두배, 10)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"20") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-3: 함수를 반환값으로", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 고르기() -> 결과 { 돌려주기 두배. }\n"
        U"변수 g = 고르기().\n"
        U"보여주기(g(9)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"18") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-3: 함수값을 레코드 필드에 (함수 1급 x 레코드)", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"변수 도구 = (연산: 두배).\n"
        U"보여주기(도구.연산(6)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"12") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 함수 1급 바인딩", "[integration][v04a3][acc-dameum]") {
    auto out = run_via_dameum(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. } 변수 f = 두배. 보여주기(f(11)).");
    REQUIRE(out.find(U"22") != std::u32string::npos);
}

// === v0.4a-3 추가 점검 — 함수 1급 엣지 케이스 10종 ===

TEST_CASE("v0.4a-3 점검: 재귀 함수를 변수에 바인딩 후 호출", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 팩토리얼(n) -> 결과 {\n"
        U"  만약 (n <= 1) 이면 { 돌려주기 1. }\n"
        U"  돌려주기 n * 팩토리얼(n - 1).\n"
        U"}\n"
        U"변수 계산 = 팩토리얼.\n"
        U"보여주기(계산(5)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"120") != std::u32string::npos);
}

TEST_CASE("v0.4a-3 점검: builtin 보여주기도 1급 값", "[integration][both][v04a3]") {
    auto b = run_both(U"변수 출력 = 보여주기. 출력(\"일급보여주기\").");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"일급보여주기") != std::u32string::npos);
}

TEST_CASE("v0.4a-3 점검: 함수 합성 바깥(안쪽(값))", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 더1(수) -> 결과 { 돌려주기 수 + 1. }\n"
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 합성(바깥, 안쪽, 값) -> 결과 { 돌려주기 바깥(안쪽(값)). }\n"
        U"보여주기(합성(두배, 더1, 4)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"10") != std::u32string::npos);   // 두배(더1(4)) = 두배(5)
}

TEST_CASE("v0.4a-3 점검: 같은 함수를 두 번 적용", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 더1(수) -> 결과 { 돌려주기 수 + 1. }\n"
        U"함수 두번(연산, 값) -> 결과 { 돌려주기 연산(연산(값)). }\n"
        U"보여주기(두번(더1, 10)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"12") != std::u32string::npos);
}

TEST_CASE("v0.4a-3 점검: 조건부 함수 선택 + 즉시 호출 (체이닝)", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 세배(수) -> 결과 { 돌려주기 수 * 3. }\n"
        U"함수 고르기(짝) -> 결과 { 만약 (짝) 이면 { 돌려주기 두배. } 돌려주기 세배. }\n"
        U"보여주기(고르기(참)(5)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"10") != std::u32string::npos);
}

TEST_CASE("v0.4a-3 점검: 함수값 == 비교는 거짓 (동등성 미정의)", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. } 보여주기(두배 == 두배).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"거짓") != std::u32string::npos);
}

TEST_CASE("v0.4a-3 점검: 레코드에 함수 2개 담아 각각 호출", "[integration][both][v04a3]") {
    auto b = run_both(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 세배(수) -> 결과 { 돌려주기 수 * 3. }\n"
        U"변수 도구 = (곱2: 두배, 곱3: 세배).\n"
        U"보여주기(도구.곱2(5)). 보여주기(도구.곱3(5)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"10") != std::u32string::npos);
    REQUIRE(b.tree.find(U"15") != std::u32string::npos);
}

// === v0.4a-4: 계약 선언형 블록 양 경로 의무 (#32, 결정 63·87·90) ===

TEST_CASE("양경로 v0.4a-4: 계약 필드 접근", "[integration][both][v04a4]") {
    auto b = run_both(
        U"계약 은행 { 주인: \"철수\" 잔액: 100 }\n"
        U"보여주기(은행.주인). 보여주기(은행.잔액).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"철수") != std::u32string::npos);
    REQUIRE(b.tree.find(U"100") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-4: 계약 메서드 호출", "[integration][both][v04a4]") {
    auto b = run_both(
        U"계약 계산기 {\n"
        U"  함수 더하기(가, 나) -> 결과 { 돌려주기 가 + 나. }\n"
        U"}\n"
        U"보여주기(계산기.더하기(2, 3)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"5") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-4: 계약 필드 + 메서드 혼합", "[integration][both][v04a4]") {
    auto b = run_both(
        U"계약 은행 {\n"
        U"  이름: \"세움은행\"\n"
        U"  함수 환영() -> 결과 { 돌려주기 \"어서오세요\". }\n"
        U"}\n"
        U"보여주기(은행.이름). 보여주기(은행.환영()).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"세움은행") != std::u32string::npos);
    REQUIRE(b.tree.find(U"어서오세요") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-4: 계약 메서드는 전역으로도 접근 (결정 90)", "[integration][both][v04a4]") {
    auto b = run_both(
        U"계약 계산기 { 함수 곱하기(가, 나) -> 결과 { 돌려주기 가 * 나. } }\n"
        U"보여주기(곱하기(4, 5)).");   // 계약 밖에서 전역 이름으로 직접 호출
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"20") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 계약 선언형 블록", "[integration][v04a4][acc-dameum]") {
    auto out = run_via_dameum(
        U"계약 창고 { 함수 개수() -> 결과 { 돌려주기 7. } } 보여주기(창고.개수()).");
    REQUIRE(out.find(U"7") != std::u32string::npos);
}

// === v0.4a-5: 자산 타입 + 금융 그릇 양 경로 의무 (#32, 결정 64) ===

TEST_CASE("양경로 v0.4a-5: 자산 += 누적", "[integration][both][v04a5]") {
    auto b = run_both(
        U"가져오기(금융). 변수 지갑 = (돈: BTC(100)). 지갑.돈 += BTC(50). 보여주기(지갑.돈).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"BTC(150)") != std::u32string::npos);
}

TEST_CASE("양경로 v0.4a-5: KRW 자산 출력 동일", "[integration][both][v04a5]") {
    auto b = run_both(U"가져오기(금융). 보여주기(KRW(5000)).");
    REQUIRE(b.tree == b.vm_);
    REQUIRE(b.tree.find(U"KRW(5000)") != std::u32string::npos);
}

TEST_CASE(".담음 빌드 경로: 자산 생성·누적", "[integration][v04a5][acc-dameum]") {
    auto out = run_via_dameum(
        U"가져오기(금융). 변수 w = (돈: BTC(7)). w.돈 += BTC(3). 보여주기(w.돈).");
    REQUIRE(out.find(U"BTC(10)") != std::u32string::npos);
}
