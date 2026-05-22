#pragma once

#include "../environment.h"
#include "builtin.h"

namespace seum::modules {

// v0.3e: native_xxx 함수들을 환경에 등록.
// `위험 { native_xxx(args) }` 안에서 호출됨.
// 등록되는 native:
//   native_보여주기(값) -> 없음                  — sink 출력 (builtin 의 보여주기와 동일 sink 공유)
//   native_시각_시스템_초() -> 정수              — 현재 SST 초 (v0.2 의 current_sst_seconds)
//
// 결정 #57·#58: v1.0 자기-호스팅 길의 첫 걸음. `.세움` 그릇이 이 native 들을 호출.
void register_natives(Environment& env, OutputSink sink = default_stdout_sink());

}  // namespace seum::modules
