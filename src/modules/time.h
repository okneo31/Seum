#pragma once

#include "../environment.h"

namespace seum::modules {

// 시간 그릇을 환경에 등록한다. 이후 `가져오기(시간).` 으로 활성화 가능.
// 활성화되면 식별자 `지금`이 GetterValue로 노출된다.
//
// 내부 시각 계산 함수 (sst_epoch_unix_seconds, current_sst_seconds) 는
// `time_internal.h` 로 분리. 일반 consumer 는 본 헤더만 포함하면 충분.
void register_time(Environment& env);

}  // namespace seum::modules
