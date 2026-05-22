#include "finance.h"

#include "../error_reporter.h"
#include "../value.h"

#include <utility>
#include <vector>

namespace seum::modules {

namespace {

// BTC()/KRW() 자산 생성 함수 — 정수 1개(최소단위)를 받아 자산값으로.
// 결정 64: 음수 금지. 세움은 정수만 → BTC=satoshi, KRW=원.
Value make_currency_ctor(std::u32string currency) {
    auto fn = std::make_shared<CallableValue>();
    fn->name = currency;
    fn->fn = [currency](std::vector<Value>& args, Position pos) -> Value {
        if (args.size() != 1) {
            raise(pos, currency + U" 는 정수 인자 1개가 필요합니다");
        }
        const std::int64_t* n = as_int(args[0]);
        if (!n) {
            raise(pos, currency + U" 의 인자는 정수여야 합니다");
        }
        if (*n < 0) {
            raise(pos, U"자산은 음수가 될 수 없습니다 (결정 64)");
        }
        return make_asset(currency, *n);
    };
    return Value{fn};
}

// 금액(자산) -> 정수 — 자산의 최소단위 정수 추출 (v0.4a-5c).
Value make_amount_fn() {
    auto fn = std::make_shared<CallableValue>();
    fn->name = U"금액";
    fn->fn = [](std::vector<Value>& args, Position pos) -> Value {
        if (args.size() != 1) raise(pos, U"금액(...) 은 인자 1개가 필요합니다");
        const AssetValue* a = as_asset(args[0]);
        if (!a) raise(pos, U"금액(...) 의 인자는 자산이어야 합니다");
        return make_int(a->amount);
    };
    return Value{fn};
}

// 통화등록("이름") -> 통화 생성 함수 — `통화 X.` 가 desugar 해서 호출 (v0.4a-5c).
Value make_register_currency_fn() {
    auto fn = std::make_shared<CallableValue>();
    fn->name = U"통화등록";
    fn->fn = [](std::vector<Value>& args, Position pos) -> Value {
        if (args.size() != 1) raise(pos, U"통화등록(...) 은 인자 1개가 필요합니다");
        const std::u32string* s = as_string(args[0]);
        if (!s) raise(pos, U"통화등록(...) 의 인자는 문자열이어야 합니다");
        return make_currency_ctor(*s);
    };
    return Value{fn};
}

}  // namespace

void register_finance(Environment& env) {
    // `가져오기(금융).` 시 통화·자산 함수를 노출 — 결정 64.
    env.add_module(U"금융", [](Environment& e) {
        e.define(U"BTC", make_currency_ctor(U"BTC"));
        e.define(U"KRW", make_currency_ctor(U"KRW"));
        e.define(U"금액", make_amount_fn());
        e.define(U"통화등록", make_register_currency_fn());
    });
}

}  // namespace seum::modules
