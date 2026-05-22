#include "value.h"

#include <string>
#include <type_traits>

namespace seum {

Value make_none() { return Value{std::monostate{}}; }
Value make_bool(bool b) { return Value{b}; }
Value make_int(std::int64_t v) { return Value{v}; }
Value make_string(std::u32string s) { return Value{std::move(s)}; }
Value make_time(std::int64_t sst_seconds) { return Value{TimeValue{sst_seconds}}; }

bool is_none(const Value& v)     { return std::holds_alternative<std::monostate>(v); }
bool is_bool(const Value& v)     { return std::holds_alternative<bool>(v); }
bool is_int(const Value& v)      { return std::holds_alternative<std::int64_t>(v); }
bool is_string(const Value& v)   { return std::holds_alternative<std::u32string>(v); }
bool is_time(const Value& v)     { return std::holds_alternative<TimeValue>(v); }
bool is_callable(const Value& v) { return std::holds_alternative<std::shared_ptr<CallableValue>>(v); }
bool is_getter(const Value& v)   { return std::holds_alternative<std::shared_ptr<GetterValue>>(v); }
bool is_record(const Value& v)   { return std::holds_alternative<std::shared_ptr<RecordValue>>(v); }

const bool*          as_bool(const Value& v)   { return std::get_if<bool>(&v); }
const std::int64_t*  as_int(const Value& v)    { return std::get_if<std::int64_t>(&v); }
const std::u32string* as_string(const Value& v) { return std::get_if<std::u32string>(&v); }
const TimeValue*     as_time(const Value& v)   { return std::get_if<TimeValue>(&v); }

const CallableValue* as_callable(const Value& v) {
    auto p = std::get_if<std::shared_ptr<CallableValue>>(&v);
    return p ? p->get() : nullptr;
}
const GetterValue* as_getter(const Value& v) {
    auto p = std::get_if<std::shared_ptr<GetterValue>>(&v);
    return p ? p->get() : nullptr;
}
RecordValue* as_record(const Value& v) {
    auto p = std::get_if<std::shared_ptr<RecordValue>>(&v);
    return p ? p->get() : nullptr;
}

Value* RecordValue::get(const std::u32string& key) {
    for (auto& f : fields) if (f.first == key) return &f.second;
    return nullptr;
}
const Value* RecordValue::get(const std::u32string& key) const {
    for (const auto& f : fields) if (f.first == key) return &f.second;
    return nullptr;
}

namespace {

std::u32string to_u32(std::int64_t n, int width = 0) {
    std::string ascii = std::to_string(n);
    while (static_cast<int>(ascii.size()) < width) ascii.insert(ascii.begin(), '0');
    std::u32string out;
    out.reserve(ascii.size());
    for (char c : ascii) out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    return out;
}

bool is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int days_in_month(int y, int m) {
    if (m == 2) return is_leap_year(y) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

// SST epoch 기준 초 → 세움력 (년/월/일/시/분/초)
// SST epoch = 1945-08-15 06:00 KST = Seum 0년 8월 15일 06:00 SST
struct SeumDateTime {
    int year, month, day, hour, minute, second;
};

SeumDateTime decompose(std::int64_t s) {
    std::int64_t days = s / 86400;
    std::int64_t rem = s % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }

    // SST epoch이 KST 06:00 시점이므로 SST 0초의 시각 표시가 06:00이 되도록
    // 분해할 때 +6시간 보정한다. (KST↔UTC 변환 아님 — epoch의 좌표 반영.)
    rem += 6 * 3600;
    if (rem >= 86400) {
        rem -= 86400;
        days += 1;
    }

    int hour   = static_cast<int>(rem / 3600);
    int minute = static_cast<int>((rem % 3600) / 60);
    int second = static_cast<int>(rem % 60);

    // 시작점: 그레고리력 1945-08-15
    int year = 1945, month = 8, day = 15;

    if (days >= 0) {
        while (days > 0) {
            int dim = days_in_month(year, month);
            int remain = dim - day;
            if (days <= remain) {
                day += static_cast<int>(days);
                days = 0;
            } else {
                days -= (remain + 1);
                day = 1;
                month++;
                if (month > 12) { month = 1; year++; }
            }
        }
    } else {
        std::int64_t back = -days;
        while (back > 0) {
            int remain = day - 1;
            if (back <= remain) {
                day -= static_cast<int>(back);
                back = 0;
            } else {
                back -= day;
                month--;
                if (month < 1) { month = 12; year--; }
                day = days_in_month(year, month);
            }
        }
    }

    return {year - 1945, month, day, hour, minute, second};
}

std::u32string format_sst(std::int64_t s) {
    SeumDateTime dt = decompose(s);
    std::u32string out;
    out.reserve(40);
    out += U"세움력 ";
    out += to_u32(dt.year);
    out += U"년 ";
    out += to_u32(dt.month);
    out += U"월 ";
    out += to_u32(dt.day);
    out += U"일 ";
    out += to_u32(dt.hour, 2);
    out += U":";
    out += to_u32(dt.minute, 2);
    out += U":";
    out += to_u32(dt.second, 2);
    out += U" SST";
    return out;
}

}  // namespace

std::u32string render_value(const Value& v) {
    return std::visit([](const auto& x) -> std::u32string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return U"없음";
        } else if constexpr (std::is_same_v<T, bool>) {
            return x ? U"참" : U"거짓";  // 결정 #27
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return to_u32(x);
        } else if constexpr (std::is_same_v<T, std::u32string>) {
            return x;
        } else if constexpr (std::is_same_v<T, TimeValue>) {
            return format_sst(x.sst_seconds);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<CallableValue>>) {
            return U"<함수 " + x->name + U">";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<GetterValue>>) {
            return U"<지연값 " + x->name + U">";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<RecordValue>>) {
            std::u32string out = U"(";
            for (std::size_t i = 0; i < x->fields.size(); ++i) {
                if (i) out += U", ";
                out += x->fields[i].first;
                out += U": ";
                out += render_value(x->fields[i].second);
            }
            out += U")";
            return out;
        }
    }, v);
}

}  // namespace seum
