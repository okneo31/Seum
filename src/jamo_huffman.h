#pragma once

#include "common.h"

// 자모 Huffman 압축 — 세움 정체성의 진한 표현 (잠긴 결정 #50).
// 한국어 텍스트의 *자모* 토큰을 추출 → 빈도 기반 Huffman.
// 라운드트립 비트 동일 보장 (acceptance 14 의 측정 기준, F11 patch).
namespace seum::jamo {

// 완성형 한글 → 자모 시퀀스.
// 음절 (U+AC00..U+D7A3) 는 [초성, 중성, 종성?] 2~3 jamo.
// 비-한글 codepoint 는 그대로 유지.
std::u32string decompose(const std::u32string& text);

// 자모 시퀀스 → 완성형 한글. 종성/다음 음절 초성 lookahead 모호성 처리.
std::u32string compose(const std::u32string& jamo_seq);

// 한국어 텍스트를 자모 Huffman 으로 압축.
std::vector<std::uint8_t> compress(const std::u32string& text);

// 압축 복원. compress 결과로부터 원본 정확히 복원.
std::u32string decompress(const std::vector<std::uint8_t>& compressed);

}  // namespace seum::jamo
