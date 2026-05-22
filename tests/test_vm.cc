#include <catch2/catch_amalgamated.hpp>

#include "bytecode.h"
#include "environment.h"
#include "error_reporter.h"
#include "ir.h"
#include "lexer.h"
#include "modules/builtin.h"
#include "modules/finance.h"
#include "modules/time.h"
#include "parser.h"
#include "vm.h"

using namespace seum;

static int compile_and_run(std::u32string_view src, Environment& env) {
    auto ir_prog = ir::lower(parse(tokenize(src)));
    auto bc_prog = bytecode::compile(ir_prog);
    return vm::run(bc_prog, env);
}

TEST_CASE("VM: 빈 Program(HALT만) 정상 종료", "[vm]") {
    bytecode::Program p;
    p.main.chunk.bytes.push_back(static_cast<std::uint8_t>(bytecode::Opcode::HALT));
    p.main.chunk.positions.push_back({});
    Environment env;
    REQUIRE(vm::run(p, env) == 0);
}

TEST_CASE("VM: 변수 선언 후 환경에 바인딩", "[vm]") {
    Environment env;
    modules::register_builtin(env);
    compile_and_run(U"변수 안녕 = \"세움\".", env);
    auto v = env.get(U"안녕");
    REQUIRE(v.has_value());
    auto* s = as_string(*v);
    REQUIRE(s != nullptr);
    REQUIRE(*s == U"세움");
}

TEST_CASE("VM: 보여주기 호출이 sink 로 출력", "[vm]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기(\"세움\").", env);

    REQUIRE(captured.find(U"세움") != std::u32string::npos);
}

TEST_CASE("VM: 가져오기 + 지금 GetterValue 자동 발화 (잠긴 결정 F4)", "[vm]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };

    Environment env;
    modules::register_builtin(env, sink);
    modules::register_time(env);
    compile_and_run(U"가져오기(시간).\n보여주기(지금).", env);

    REQUIRE(captured.find(U"세움력") != std::u32string::npos);
    REQUIRE(captured.find(U"SST")    != std::u32string::npos);
}

TEST_CASE("VM: 알 수 없는 그릇 가져오기는 한국어 에러", "[vm]") {
    Environment env;
    modules::register_builtin(env);
    try {
        compile_and_run(U"가져오기(없는것).", env);
        FAIL("예외가 던져졌어야 합니다");
    } catch (const SeumError& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("알 수 없는 그릇") != std::string::npos);
    }
}

TEST_CASE("VM: 시간 그릇 없이 지금 사용은 한국어 에러 (acceptance 3, F15)", "[vm]") {
    Environment env;
    modules::register_builtin(env);
    modules::register_time(env);  // 그릇 등록은 하되 가져오기 안 함
    try {
        compile_and_run(U"보여주기(지금).", env);
        FAIL("예외가 던져졌어야 합니다");
    } catch (const SeumError& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("이름을 찾을 수 없습니다") != std::string::npos);
    }
}

// === v0.2b 양 경로 의무: VM 도 Bool/cmp/branch 지원 ===

TEST_CASE("VM v0.2b: 참/거짓 리터럴", "[vm][v02b]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기(참).", env);
    REQUIRE(captured.find(U"참") != std::u32string::npos);
}

TEST_CASE("VM v0.2b: 정수 비교는 Bool 푸시", "[vm][v02b]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기(3 < 5).", env);
    REQUIRE(captured.find(U"참") != std::u32string::npos);
}

TEST_CASE("VM v0.2b: 만약/아니면 분기", "[vm][v02b]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(
        U"만약 (1 == 1) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }", env);
    REQUIRE(captured.find(U"같음") != std::u32string::npos);
    REQUIRE(captured.find(U"다름") == std::u32string::npos);
}

TEST_CASE("VM v0.2b: 단락 평가 &&", "[vm][v02b]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기(참 && 거짓).", env);
    REQUIRE(captured.find(U"거짓") != std::u32string::npos);
}

TEST_CASE("VM v0.2b: 단항 NOT", "[vm][v02b]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기(!거짓).", env);
    REQUIRE(captured.find(U"참") != std::u32string::npos);
}

// === v0.4a-1 2b: 레코드·멤버 접근 — VM 경로 (결정 69·79·92) ===

TEST_CASE("VM v0.4a-1: 레코드 멤버 읽기", "[vm][v04a]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"변수 위치 = (ㄱ: 10, ㅅ: 20). 보여주기(위치.ㄱ). 보여주기(위치.ㅅ).", env);
    REQUIRE(captured.find(U"10") != std::u32string::npos);
    REQUIRE(captured.find(U"20") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-1: 중첩 레코드 멤버 체이닝", "[vm][v04a]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 보여주기(용사.위치.ㅅ).", env);
    REQUIRE(captured.find(U"20") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-1: 레코드 출력 형태", "[vm][v04a]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"보여주기((ㄱ: 1, ㅅ: 2)).", env);
    REQUIRE(captured.find(U"(ㄱ: 1, ㅅ: 2)") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-1: 없는 멤버 접근은 한국어 에러", "[vm][v04a]") {
    Environment env;
    modules::register_builtin(env);
    REQUIRE_THROWS_AS(compile_and_run(U"변수 r = (ㄱ: 1). 보여주기(r.ㅎ).", env), SeumError);
}

TEST_CASE("VM v0.4a-1: 단일 필드 레코드", "[vm][v04a]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"변수 r = (점수: 100). 보여주기(r.점수).", env);
    REQUIRE(captured.find(U"100") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-1: 비-레코드 멤버 접근은 한국어 에러", "[vm][v04a]") {
    Environment env;
    modules::register_builtin(env);
    REQUIRE_THROWS_AS(compile_and_run(U"변수 x = 5. 보여주기(x.ㄱ).", env), SeumError);
}

// === v0.4a-2: 필드 대입 + 복합대입 — VM 경로 (결정 81·93) ===

TEST_CASE("VM v0.4a-2: 복합대입 +=", "[vm][v04a2]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(
        U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 용사.위치.ㄱ += 10. 보여주기(용사.위치.ㄱ).", env);
    REQUIRE(captured.find(U"20") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-2: 참조 의미론 별칭 변이 (결정 93)", "[vm][v04a2]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(U"변수 가 = (ㄱ: 1). 변수 나 = 가. 나.ㄱ = 99. 보여주기(가.ㄱ).", env);
    REQUIRE(captured.find(U"99") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-2: 없는 멤버 대입은 한국어 에러", "[vm][v04a2]") {
    Environment env;
    modules::register_builtin(env);
    REQUIRE_THROWS_AS(compile_and_run(U"변수 r = (ㄱ: 1). r.ㅎ = 5.", env), SeumError);
}

// === v0.4a-3: 함수 1급 — VM 경로 (결정 62) ===

TEST_CASE("VM v0.4a-3: 함수 1급 변수 바인딩", "[vm][v04a3]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(
        U"함수 세배(수) -> 결과 { 돌려주기 수 * 3. } 변수 f = 세배. 보여주기(f(4)).", env);
    REQUIRE(captured.find(U"12") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-3 점검: 레코드에 담은 함수를 인자로 전달", "[vm][v04a3]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    compile_and_run(
        U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 꺼내실행(상자, 값) -> 결과 { 돌려주기 상자.연산(값). }\n"
        U"변수 상자 = (연산: 두배).\n"
        U"보여주기(꺼내실행(상자, 9)).", env);
    REQUIRE(captured.find(U"18") != std::u32string::npos);
}

// === v0.4a-5: 자산 타입 — VM 경로 (결정 64) ===

TEST_CASE("VM v0.4a-5: KRW 자산 생성·출력", "[vm][v04a5]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    modules::register_finance(env);
    compile_and_run(U"가져오기(금융). 보여주기(KRW(1000)).", env);
    REQUIRE(captured.find(U"KRW(1000)") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-5: 자산 += 누적", "[vm][v04a5]") {
    std::u32string captured;
    auto sink = [&captured](const std::u32string& s) { captured += s; };
    Environment env;
    modules::register_builtin(env, sink);
    modules::register_finance(env);
    compile_and_run(
        U"가져오기(금융). 변수 w = (돈: BTC(10)). w.돈 += BTC(5). 보여주기(w.돈).", env);
    REQUIRE(captured.find(U"BTC(15)") != std::u32string::npos);
}

TEST_CASE("VM v0.4a-5: 통화 불일치는 한국어 에러", "[vm][v04a5]") {
    Environment env;
    modules::register_builtin(env);
    modules::register_finance(env);
    REQUIRE_THROWS_AS(
        compile_and_run(U"가져오기(금융). 변수 w = (돈: BTC(1)). w.돈 += KRW(2).", env),
        SeumError);
}
