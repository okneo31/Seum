#pragma once

#include "bytecode.h"
#include "environment.h"

namespace seum::vm {

// 바이트코드 Program 을 환경 env 위에서 실행.
// 1. prog.functions 의 각 함수를 CallableValue 로 env 에 등록.
// 2. prog.main 의 Chunk 를 실행 (HALT 까지).
// 함수 호출 시 새 frame (지역 변수 배열 + 데이터 스택은 공유 옵션이지만 본 구현은 공유).
int run(const bytecode::Program& prog, Environment& env);

// v0.3e: prog 의 함수들만 env 에 등록 (main 실행 X). `가져오기` 의 .담음 로드 경로용.
// prog 는 env 보다 오래 살아야 함 (caller 가 보장).
void register_program(const bytecode::Program& prog, Environment& env);

}  // namespace seum::vm
