#include <catch2/catch_amalgamated.hpp>

#include "modules/time.h"
#include "modules/time_internal.h"
#include "value.h"

using namespace seum;
using namespace seum::modules;

TEST_CASE("시간: SST epoch는 1945-08-15 06:00 KST", "[time]") {
    // 1945-08-15 06:00 KST = 1945-08-14 21:00 UTC.
    // 그 시점의 unix 초는 -769402800.
    REQUIRE(sst_epoch_unix_seconds() == -769402800);
}

TEST_CASE("시간: epoch 직후 포맷", "[time]") {
    // SST 초 0  =  1945-08-15 06:00 KST  →  '세움력 0년 8월 15일 06:00:00 SST'
    auto rendered = render_value(make_time(0));
    REQUIRE(rendered.find(U"세움력 0년 8월 15일 06:00:00 SST") != std::u32string::npos);
}

TEST_CASE("시간: 하루 후", "[time]") {
    auto rendered = render_value(make_time(86400));
    REQUIRE(rendered.find(U"8월 16일") != std::u32string::npos);
}

TEST_CASE("시간: 한 해 후 (윤년 아님)", "[time]") {
    // 1945-08-15 + 365일 = 1946-08-15 → 세움력 1년 8월 15일
    auto rendered = render_value(make_time(365LL * 86400));
    REQUIRE(rendered.find(U"1년 8월 15일") != std::u32string::npos);
}

TEST_CASE("시간: 시각 포맷", "[time]") {
    // SST 3h14m07s  →  epoch(06:00) + 03:14:07  →  09:14:07
    auto rendered = render_value(make_time(3 * 3600 + 14 * 60 + 7));
    REQUIRE(rendered.find(U"09:14:07") != std::u32string::npos);
}

TEST_CASE("시간: 자정 캐리 (epoch + 18시간 = 다음 날 00:00)", "[time]") {
    // SST 18h  →  epoch(06:00) + 18h  =  다음 날 00:00.  날짜 8월 16일.
    auto rendered = render_value(make_time(18 * 3600));
    REQUIRE(rendered.find(U"8월 16일 00:00:00") != std::u32string::npos);
}
