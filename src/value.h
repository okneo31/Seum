#pragma once

#include "common.h"

#include <functional>
#include <utility>

namespace seum {

struct CallableValue;
struct GetterValue;
struct RecordValue;

// SST(세움 표준시) 기준 epoch부터의 초 단위.
struct TimeValue {
    std::int64_t sst_seconds;
};

// v0.4a-5: 자산 값 (BTC·KRW) — 결정 64. amount = 최소단위 정수
// (BTC=satoshi, KRW=원). 음수 금지. 값 의미론 (복사) — int·TimeValue 와 동류.
struct AssetValue {
    std::u32string currency;
    std::int64_t   amount;
};

using Value = std::variant<
    std::monostate,
    bool,                                // v0.2b: 결정 #25 (참/거짓)
    std::int64_t,
    std::u32string,
    TimeValue,
    AssetValue,                          // v0.4a-5: 결정 #64
    std::shared_ptr<CallableValue>,
    std::shared_ptr<GetterValue>,
    std::shared_ptr<RecordValue>
>;

struct CallableValue {
    std::u32string name;
    std::function<Value(std::vector<Value>&, Position)> fn;
};

// 식별자 조회 시 자동 실행되는 'live' 값. `지금` 처리 등.
struct GetterValue {
    std::u32string name;
    std::function<Value()> fn;
};

// 범용 자모-키 레코드 — 결정 #69. (ㄱ: 10, ㅅ: 20) 같은 값.
// shared_ptr 로 보관 → 참조 의미 (멤버 대입이 별칭에 반영, v0.4a-2 예정).
struct RecordValue {
    std::vector<std::pair<std::u32string, Value>> fields;
    Value*       get(const std::u32string& key);
    const Value* get(const std::u32string& key) const;
};

Value make_none();
Value make_bool(bool b);
Value make_int(std::int64_t v);
Value make_string(std::u32string s);
Value make_time(std::int64_t sst_seconds);
Value make_asset(std::u32string currency, std::int64_t amount);

// === 변형 질의 헬퍼 ===
// consumer 코드는 std::get_if / std::holds_alternative 를 직접 호출하지 않는다.
bool is_none(const Value& v);
bool is_bool(const Value& v);
bool is_int(const Value& v);
bool is_string(const Value& v);
bool is_time(const Value& v);
bool is_asset(const Value& v);
bool is_callable(const Value& v);
bool is_getter(const Value& v);
bool is_record(const Value& v);

const bool* as_bool(const Value& v);
const std::int64_t* as_int(const Value& v);
const std::u32string* as_string(const Value& v);
const TimeValue* as_time(const Value& v);
const AssetValue* as_asset(const Value& v);
const CallableValue* as_callable(const Value& v);
const GetterValue* as_getter(const Value& v);
RecordValue* as_record(const Value& v);

// '보여주기' 출력용 문자열 표현. SST 시각은 세움력 포맷으로.
std::u32string render_value(const Value& v);

}  // namespace seum
