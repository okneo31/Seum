#pragma once

#include "../environment.h"

namespace seum::modules {

// 금융 그릇을 환경에 등록한다. 이후 `가져오기(금융).` 으로 활성화 가능.
// 활성화되면 자산 생성 함수 BTC()·KRW() 가 식별자로 노출된다. — 결정 64.
void register_finance(Environment& env);

}  // namespace seum::modules
