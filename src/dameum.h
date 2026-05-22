#pragma once

#include "common.h"

// 담음 (.담음) — 세움세상의 평화를 위한 그릇.
// 명세 v1.5 §4 의 binary layout 구현. 잠긴 결정 41–48, 56.
// v0.3a 골격: 포맷 정의 + reader/writer. chunk 안 내용 (코드/한글텍스트 등) 은 v0.3b+.
namespace seum::dameum {

// 자모 매핑 (명세 §5, F5 patch). byte 값과 한글 자모 1:1.
enum class ChunkType : std::uint8_t {
    Code         = 0x01,  // ㄱ — 그릇 / 기능 (bytecode)
    Name         = 0x02,  // ㄴ — 이름 (시그니처·export)
    Meta         = 0x03,  // ㄷ — 담음 메타데이터
    State        = 0x04,  // ㅁ — 마음 (상태)
    Media        = 0x05,  // ㅂ — 빛 (미디어)
    Source       = 0x06,  // ㅅ — 소스 (.세움 원본)
    Link         = 0x07,  // ㅇ — 이음 (외부 .담음 링크)
    KoreanText   = 0x08,  // ㅎ — 한국어 텍스트
};

enum class Compression : std::uint8_t {
    Raw          = 0,
    Zlib         = 1,    // v0.3+ 옵셔널
    JamoHuffman  = 2,    // v0.3c 한글텍스트 chunk 전용
};

// Header flags (명세 §4.1).
constexpr std::uint16_t FLAG_COMPRESSED       = 0x0001;
constexpr std::uint16_t FLAG_BAREMETAL_READY  = 0x0002;
constexpr std::uint16_t FLAG_HAS_MORPHEMES    = 0x0004;
constexpr std::uint16_t FLAG_HAS_EXTERNAL     = 0x0008;

constexpr std::uint16_t DAMEUM_VERSION_V03    = 0x0001;
constexpr std::uint32_t HEADER_SIZE           = 16;
constexpr std::uint32_t CHUNK_TABLE_ENTRY_SIZE = 16;
constexpr std::uint32_t ALIGNMENT             = 16;

// 한 chunk 본문 + 메타.
// `data` 는 *압축 형태* 로 보관. uncompressed_size 는 압축 전 크기 (raw 면 data.size() 와 동일).
struct Chunk {
    ChunkType                  type;
    Compression                compression{Compression::Raw};
    std::uint16_t              flags{0};
    std::vector<std::uint8_t>  data;
    std::uint32_t              uncompressed_size{0};
};

struct Container {
    std::uint16_t       version{DAMEUM_VERSION_V03};
    std::uint16_t       flags{0};
    std::vector<Chunk>  chunks;
};

// 디스크 → 메모리. UTF-8 path. 실패 시 SeumError 던짐.
Container read(const std::string& utf8_path);

// 메모리 → 디스크. UTF-8 path. 16-byte alignment 보장.
void write(const Container& c, const std::string& utf8_path);

// 한글 자모 disassemble (디버그용). 명세 §10.1.
std::u32string disassemble(const Container& c);

// 자모 글리프 표시. ChunkType::Code → U'ㄱ' 등.
char32_t chunk_type_jamo(ChunkType t);

// chunk 의미명 ("코드"/"이름"/...).
const char32_t* chunk_type_name(ChunkType t);

}  // namespace seum::dameum
