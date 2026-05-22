#include "time.h"
#include "time_internal.h"

#include "../value.h"

#include <chrono>

namespace seum::modules {

std::int64_t sst_epoch_unix_seconds() {
    // 1945-08-15 06:00 KST = 1945-08-14 21:00:00 UTC.
    // unix epoch(1970-01-01 UTC)보다 8906일 - 3시간 앞.
    //   8906 * 86400 - 21 * 3600 = 769,478,400 - 75,600 = 769,402,800
    // 주의: MSVC의 _mkgmtime은 1970년 이전(tm_year < 70)에 대해 -1 반환하므로
    // 동적 계산 대신 사전 산출 상수를 사용.
    return -769402800;
}

std::int64_t current_sst_seconds() {
    auto now = std::chrono::system_clock::now();
    std::int64_t unix_s = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    return unix_s - sst_epoch_unix_seconds();
}

void register_time(Environment& env) {
    env.add_module(U"시간", [](Environment& e) {
        auto getter = std::make_shared<GetterValue>();
        getter->name = U"지금";
        getter->fn = []() {
            return make_time(current_sst_seconds());
        };
        e.define(U"지금", Value{getter});
    });
}

}  // namespace seum::modules
