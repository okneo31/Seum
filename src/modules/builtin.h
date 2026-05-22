#pragma once

#include "../environment.h"

#include <functional>

namespace seum::modules {

// 출력 싱크. '보여주기'가 1회 호출 시 (개행 포함된) 1개의 u32string으로 호출.
using OutputSink = std::function<void(const std::u32string&)>;

// 기본 sink: UTF-8로 변환하여 stdout에 출력.
OutputSink default_stdout_sink();

// 항상 사용 가능한 기본 그릇 (보여주기 등). 환경 부팅 시 즉시 등록.
// sink를 지정하면 보여주기 출력이 그 sink로 향한다. 미지정 시 stdout.
void register_builtin(Environment& env, OutputSink sink = default_stdout_sink());

}  // namespace seum::modules
