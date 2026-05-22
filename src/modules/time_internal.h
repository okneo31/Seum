#pragma once

// detail 헤더. `시간` 그릇의 내부 시각 계산 함수들.
// 일반 consumer 는 `time.h` 의 `register_time` 만 호출한다.
// 본 헤더는 테스트 및 `time.cc` 자신만 포함한다.

#include "../common.h"

namespace seum::modules {

// SST epoch (1945-08-15 06:00 KST = 1945-08-14 21:00 UTC) 의 unix 초.
// = -769402800. MSVC `_mkgmtime` 이 1970년 이전을 처리 못 하므로 정적 상수.
std::int64_t sst_epoch_unix_seconds();

// 현재 시각을 SST 초(epoch 기준) 로 반환.
std::int64_t current_sst_seconds();

}  // namespace seum::modules
