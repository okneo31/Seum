#include <catch2/catch_amalgamated.hpp>

#include "environment.h"
#include "error_reporter.h"
#include "interpreter.h"
#include "lexer.h"
#include "modules/builtin.h"
#include "modules/finance.h"
#include "modules/native_bridge.h"
#include "modules/time.h"
#include "parser.h"
#include "utf8.h"

using namespace seum;

namespace {

// 출력 캡처용 헬퍼. 보여주기에 sink를 주입하여 u32 버퍼에 누적.
struct Captured {
    std::u32string buffer;
    modules::OutputSink sink() {
        return [this](const std::u32string& s) { buffer += s; };
    }
    std::string utf8() const { return utf8::encode(buffer); }
};

Environment make_env(modules::OutputSink sink) {
    Environment env;
    modules::register_builtin(env, sink);
    modules::register_time(env);
    modules::register_natives(env, std::move(sink));   // v0.3e
    modules::register_finance(env);                    // v0.4a-5
    return env;
}

// 시간 그릇 없이 평가 (지금 사용 시 에러를 발생시키기 위한 환경).
Environment make_env_no_time(modules::OutputSink sink) {
    Environment env;
    modules::register_builtin(env, std::move(sink));
    return env;
}

void run(const std::u32string& src, Environment& env) {
    auto toks = tokenize(src);
    auto prog = parse(toks);
    evaluate(prog, env);
}

}  // namespace

TEST_CASE("해석기: 문자열 출력", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(\"세움\").", env);
    REQUIRE(cap.buffer.find(U"세움") != std::u32string::npos);
}

TEST_CASE("해석기: 변수 출력", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 안녕 = \"세움\". 보여주기(안녕).", env);
    REQUIRE(cap.buffer.find(U"세움") != std::u32string::npos);
}

TEST_CASE("해석기: 시간 그릇 미가져오면 오류", "[interpreter]") {
    Captured cap;
    Environment env = make_env_no_time(cap.sink());
    REQUIRE_THROWS_AS(run(U"보여주기(지금).", env), SeumError);
}

TEST_CASE("해석기: 시간 그릇 가져오면 지금 동작", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_NOTHROW(run(U"가져오기(시간). 변수 t = 지금.", env));
}

TEST_CASE("해석기: 정수 출력", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(42).", env);
    REQUIRE(cap.buffer.find(U"42") != std::u32string::npos);
}

TEST_CASE("해석기: 출력은 줄바꿈으로 끝난다", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(\"한줄\").", env);
    REQUIRE_FALSE(cap.buffer.empty());
    REQUIRE(cap.buffer.back() == U'\n');
}

TEST_CASE("해석기: 같은 환경에서 보여주기 누적", "[interpreter]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(\"하나\"). 보여주기(\"둘\").", env);
    REQUIRE(cap.buffer.find(U"하나") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"둘") != std::u32string::npos);
}

// === v0.2b: Bool + 비교 + 논리 + 만약/아니면 ===

TEST_CASE("해석기 v0.2b: 참/거짓 리터럴", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(참). 보여주기(거짓).", env);
    REQUIRE(cap.buffer.find(U"참")    != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"거짓")  != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 정수 동등 비교", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(1 == 1). 보여주기(1 == 2).", env);
    REQUIRE(cap.buffer.find(U"참")   != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"거짓") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 대소 비교", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(3 < 5). 보여주기(5 <= 5). 보여주기(7 > 2).", env);
    // 셋 다 참이어야 함
    REQUIRE(cap.buffer.find(U"거짓") == std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 단락 평가 &&", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(참 && 거짓). 보여주기(참 && 참).", env);
    REQUIRE(cap.buffer.find(U"거짓") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"참")   != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 단락 평가 ||", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(거짓 || 참). 보여주기(거짓 || 거짓).", env);
    REQUIRE(cap.buffer.find(U"참")   != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"거짓") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 단항 NOT", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(!참). 보여주기(!거짓).", env);
    REQUIRE(cap.buffer.find(U"거짓") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"참")   != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 만약/아니면 — 참 분기", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"만약 (1 == 1) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }", env);
    REQUIRE(cap.buffer.find(U"같음") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"다름") == std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 만약/아니면 — 거짓 분기", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"만약 (1 == 2) 이면 { 보여주기(\"같음\"). } 아니면 { 보여주기(\"다름\"). }", env);
    REQUIRE(cap.buffer.find(U"다름") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"같음") == std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 만약 단독 (else 없음)", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"만약 (3 < 5) 이면 { 보여주기(\"OK\"). }", env);
    REQUIRE(cap.buffer.find(U"OK") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2b: 비교 타입 불일치는 에러", "[interpreter][v02b]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"보여주기(1 < \"x\").", env), SeumError);
}

// === v0.2c: 산술 + 문자열 결합 ===

TEST_CASE("해석기 v0.2c: 산술 우선순위", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(1 + 2 * 3). 보여주기((1 + 2) * 3).", env);
    REQUIRE(cap.buffer.find(U"7") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"9") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2c: 단항 음수", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(-5). 보여주기(-(3 + 2)).", env);
    REQUIRE(cap.buffer.find(U"-5") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2c: 문자열 결합", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(\"세움\" + \" 안녕\").", env);
    REQUIRE(cap.buffer.find(U"세움 안녕") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2c: 정수+문자열은 타입 에러 (#31)", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"보여주기(1 + \"x\").", env), SeumError);
}

TEST_CASE("해석기 v0.2c: 0으로 나눗셈은 한국어 에러", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    try {
        run(U"보여주기(7 / 0).", env);
        FAIL("예외가 던져졌어야 합니다");
    } catch (const SeumError& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("0으로 나눌 수 없습니다") != std::string::npos);
    }
}

TEST_CASE("해석기 v0.2c: 나머지 연산", "[interpreter][v02c]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(7 % 3).", env);
    REQUIRE(cap.buffer.find(U"1") != std::u32string::npos);
}

// === v0.2d: 반복문 ===

TEST_CASE("해석기 v0.2d: 반복 N번", "[interpreter][v02d]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"반복 3번 { 보여주기(\"야!\"). }", env);
    // "야!" 3번 출현
    std::size_t count = 0;
    std::size_t i = 0;
    while ((i = cap.buffer.find(U"야!", i)) != std::u32string::npos) { ++count; i += 2; }
    REQUIRE(count == 3);
}

TEST_CASE("해석기 v0.2d: 반복 0번 = 실행 안 함", "[interpreter][v02d]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"반복 0번 { 보여주기(\"아니야\"). }", env);
    REQUIRE(cap.buffer.find(U"아니야") == std::u32string::npos);
}

TEST_CASE("해석기 v0.2d: 동안 카운터 루프", "[interpreter][v02d]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 i = 0. 동안 (i < 3) { 보여주기(i). 변수 i = i + 1. }", env);
    REQUIRE(cap.buffer.find(U"0") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"1") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"2") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"3") == std::u32string::npos);
}

// === v0.3e: 위험 식 부분 도입 ===

TEST_CASE("해석기 v0.3e: 위험 { 식 } 는 inner 평가 (v0.3 marker)", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기(위험 { 1 + 2 }).", env);
    REQUIRE(cap.buffer.find(U"3") != std::u32string::npos);
}

TEST_CASE("해석기 v0.3e: 위험 { 식별자 } 도 평가", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 x = 42. 보여주기(위험 { x }).", env);
    REQUIRE(cap.buffer.find(U"42") != std::u32string::npos);
}

TEST_CASE("해석기 v0.3e: 지연값 = 자동 발화", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"지연값 항상삼() -> 결과 { 돌려주기 3. }\n"
        U"보여주기(항상삼).", env);
    REQUIRE(cap.buffer.find(U"3") != std::u32string::npos);
}

TEST_CASE("해석기 v0.3e: 지연값 + 위험 조합 (시간 그릇 시뮬)", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 카운터 = 0.\n"
        U"지연값 다음번호() -> 결과 {\n"
        U"  변수 카운터 = 카운터 + 1.\n"
        U"  돌려주기 카운터.\n"
        U"}\n"
        U"보여주기(다음번호).", env);
    REQUIRE(cap.buffer.find(U"1") != std::u32string::npos);
}

TEST_CASE("해석기 v0.3e: 위험 { native_보여주기() } 직접 호출", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"위험 { native_보여주기(\"native!\") }.", env);
    REQUIRE(cap.buffer.find(U"native!") != std::u32string::npos);
}

TEST_CASE("해석기 v0.3e: 위험 { native_시각_시스템_초() } 반환", "[interpreter][v03e]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    // 시각이 양의 SST 초 (1945-08-15 이후) — 큰 양수
    run(U"변수 초 = 위험 { native_시각_시스템_초() }. 보여주기(초 > 0).", env);
    REQUIRE(cap.buffer.find(U"참") != std::u32string::npos);
}

TEST_CASE("해석기 v0.2d: 반복 안에 만약 중첩", "[interpreter][v02d]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 i = 0. 반복 5번 { 만약 (i == 2) 이면 { 보여주기(\"hit\"). } 변수 i = i + 1. }", env);
    // i가 0..4 일 때 한 번만 hit
    std::size_t count = 0;
    std::size_t at = 0;
    while ((at = cap.buffer.find(U"hit", at)) != std::u32string::npos) { ++count; at += 3; }
    REQUIRE(count == 1);
}

// === v0.4a-1: 레코드 + 멤버 접근 (결정 69·79·92) — 트리 경로 ===

TEST_CASE("해석기 v0.4a-1: 레코드 멤버 읽기", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 위치 = (ㄱ: 10, ㅅ: 20). 보여주기(위치.ㄱ). 보여주기(위치.ㅅ).", env);
    REQUIRE(cap.buffer.find(U"10") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"20") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-1: 중첩 레코드 멤버 체이닝", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 보여주기(용사.위치.ㄱ).", env);
    REQUIRE(cap.buffer.find(U"10") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-1: 레코드 출력 형태", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"보여주기((ㄱ: 1, ㅅ: 2)).", env);
    REQUIRE(cap.buffer.find(U"(ㄱ: 1, ㅅ: 2)") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-1: 없는 멤버 접근은 한국어 에러 (4-9)", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"변수 r = (ㄱ: 1). 보여주기(r.ㅎ).", env), SeumError);
}

TEST_CASE("해석기 v0.4a-1: 레코드 아닌 값 멤버 접근은 에러", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"변수 x = 5. 보여주기(x.ㄱ).", env), SeumError);
}

TEST_CASE("해석기 v0.4a-1: 레코드 필드 값으로 식 사용", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 r = (합: 1 + 2 * 3). 보여주기(r.합).", env);
    REQUIRE(cap.buffer.find(U"7") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-1: 함수가 반환한 레코드에 멤버 접근", "[interpreter][v04a]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"함수 점만들기() -> 결과 { 돌려주기 (ㄱ: 3, ㅅ: 4). }\n"
        U"보여주기(점만들기().ㄱ).", env);
    REQUIRE(cap.buffer.find(U"3") != std::u32string::npos);
}

// === v0.4a-2: 필드 대입 + 복합대입 (결정 81·93) ===

TEST_CASE("해석기 v0.4a-2: 필드 대입 =", "[interpreter][v04a2]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 용사.위치.ㄱ = 0. 보여주기(용사.위치.ㄱ).", env);
    REQUIRE(cap.buffer.find(U"0") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-2: 복합대입 +=", "[interpreter][v04a2]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 용사 = (위치: (ㄱ: 10, ㅅ: 20)). 용사.위치.ㄱ += 10. 보여주기(용사.위치.ㄱ).", env);
    REQUIRE(cap.buffer.find(U"20") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-2: 참조 의미론 — 별칭에 변이 반영 (결정 93)", "[interpreter][v04a2]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 가 = (ㄱ: 1). 변수 나 = 가. 나.ㄱ = 99. 보여주기(가.ㄱ).", env);
    REQUIRE(cap.buffer.find(U"99") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-2: 문자열 필드 += 결합", "[interpreter][v04a2]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"변수 r = (말: \"안녕\"). r.말 += \" 세움\". 보여주기(r.말).", env);
    REQUIRE(cap.buffer.find(U"안녕 세움") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-2: 없는 멤버 대입은 한국어 에러", "[interpreter][v04a2]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"변수 r = (ㄱ: 1). r.ㅎ = 5.", env), SeumError);
}

// === v0.4a-3: 함수 1급 (결정 62) ===

TEST_CASE("해석기 v0.4a-3: 함수값 출력 형태", "[interpreter][v04a3]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. } 보여주기(두배).", env);
    REQUIRE(cap.buffer.find(U"함수") != std::u32string::npos);
    REQUIRE(cap.buffer.find(U"두배") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-3 점검: 함수 변수 재바인딩", "[interpreter][v04a3]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 세배(수) -> 결과 { 돌려주기 수 * 3. }\n"
        U"변수 f = 두배. 변수 f = 세배.\n"
        U"보여주기(f(4)).", env);
    REQUIRE(cap.buffer.find(U"12") != std::u32string::npos);   // 세배(4)
}

TEST_CASE("해석기 v0.4a-3 점검: 함수를 두 계층에 걸쳐 전달", "[interpreter][v04a3]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"함수 두배(수) -> 결과 { 돌려주기 수 * 2. }\n"
        U"함수 전달(연산, 값) -> 결과 { 돌려주기 연산(값). }\n"
        U"함수 중계(연산, 값) -> 결과 { 돌려주기 전달(연산, 값). }\n"
        U"보여주기(중계(두배, 21)).", env);
    REQUIRE(cap.buffer.find(U"42") != std::u32string::npos);
}

// === v0.4a-5: 자산 타입 + 금융 그릇 (결정 64) ===

TEST_CASE("해석기 v0.4a-5: BTC 자산 생성·출력", "[interpreter][v04a5]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"가져오기(금융). 보여주기(BTC(50000)).", env);
    REQUIRE(cap.buffer.find(U"BTC(50000)") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-5: 자산 += 누적 (레코드 필드)", "[interpreter][v04a5]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    run(U"가져오기(금융). 변수 지갑 = (돈: BTC(100)). 지갑.돈 += BTC(50). 보여주기(지갑.돈).", env);
    REQUIRE(cap.buffer.find(U"BTC(150)") != std::u32string::npos);
}

TEST_CASE("해석기 v0.4a-5: 음수 자산은 한국어 에러 (결정 64)", "[interpreter][v04a5]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"가져오기(금융). 보여주기(BTC(-5)).", env), SeumError);
}

TEST_CASE("해석기 v0.4a-5: 통화 다른 자산 더하면 에러", "[interpreter][v04a5]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(
        run(U"가져오기(금융). 변수 w = (돈: BTC(1)). w.돈 += KRW(1).", env), SeumError);
}

TEST_CASE("해석기 v0.4a-5: 가져오기(금융) 없이 BTC 사용은 에러", "[interpreter][v04a5]") {
    Captured cap;
    Environment env = make_env(cap.sink());
    REQUIRE_THROWS_AS(run(U"보여주기(BTC(1)).", env), SeumError);
}
