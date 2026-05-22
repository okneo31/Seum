#include <catch2/catch_amalgamated.hpp>

#include "dameum.h"
#include "error_reporter.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace seum;
using namespace seum::dameum;

namespace {

// 임시 디렉터리 안의 .담음 경로 (UTF-8).
// Windows pitfall #6: path::string() 은 ANSI(CP949) 변환 — 한글 매핑 실패.
// path::u8string() 으로 UTF-8 직접 얻기. u8path 로 한글 name 안전 결합.
std::string temp_path(const char* name_utf8) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path();
    p /= fs::u8path(name_utf8);
    return p.u8string();
}

}  // namespace

TEST_CASE("담음: chunk_type_jamo 한글 자모 매핑", "[dameum]") {
    REQUIRE(chunk_type_jamo(ChunkType::Code)       == U'ㄱ');
    REQUIRE(chunk_type_jamo(ChunkType::Name)       == U'ㄴ');
    REQUIRE(chunk_type_jamo(ChunkType::Meta)       == U'ㄷ');
    REQUIRE(chunk_type_jamo(ChunkType::State)      == U'ㅁ');
    REQUIRE(chunk_type_jamo(ChunkType::Media)      == U'ㅂ');
    REQUIRE(chunk_type_jamo(ChunkType::Source)     == U'ㅅ');
    REQUIRE(chunk_type_jamo(ChunkType::Link)       == U'ㅇ');
    REQUIRE(chunk_type_jamo(ChunkType::KoreanText) == U'ㅎ');
}

TEST_CASE("담음: chunk_type_name 한글 의미명", "[dameum]") {
    REQUIRE(std::u32string(chunk_type_name(ChunkType::Code))       == U"코드");
    REQUIRE(std::u32string(chunk_type_name(ChunkType::KoreanText)) == U"한글텍스트");
}

TEST_CASE("담음: 빈 Container round-trip", "[dameum]") {
    Container in;
    in.version = DAMEUM_VERSION_V03;
    auto path = temp_path("test_empty.담음");
    write(in, path);

    Container out = read(path);
    REQUIRE(out.version == DAMEUM_VERSION_V03);
    REQUIRE(out.chunks.empty());
}

TEST_CASE("담음: 단일 chunk round-trip 비트 동일", "[dameum]") {
    Container in;
    Chunk ch;
    ch.type = ChunkType::Meta;
    ch.compression = Compression::Raw;
    ch.data = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
    ch.uncompressed_size = static_cast<std::uint32_t>(ch.data.size());
    in.chunks.push_back(std::move(ch));

    auto path = temp_path("test_single.담음");
    write(in, path);
    Container out = read(path);

    REQUIRE(out.chunks.size() == 1);
    REQUIRE(out.chunks[0].type == ChunkType::Meta);
    REQUIRE(out.chunks[0].data == in.chunks[0].data);
    REQUIRE(out.chunks[0].uncompressed_size == in.chunks[0].uncompressed_size);
}

TEST_CASE("담음: 여러 chunk round-trip", "[dameum]") {
    Container in;
    for (std::uint8_t i = 0; i < 5; ++i) {
        Chunk ch;
        ch.type = static_cast<ChunkType>(0x01 + i);
        ch.data.assign(7 + i * 3, static_cast<std::uint8_t>(i * 17 + 1));
        ch.uncompressed_size = static_cast<std::uint32_t>(ch.data.size());
        in.chunks.push_back(std::move(ch));
    }
    auto path = temp_path("test_multi.담음");
    write(in, path);
    Container out = read(path);

    REQUIRE(out.chunks.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        REQUIRE(out.chunks[i].type == in.chunks[i].type);
        REQUIRE(out.chunks[i].data == in.chunks[i].data);
    }
}

TEST_CASE("담음: 잘못된 magic 은 한국어 에러", "[dameum]") {
    auto path = temp_path("test_bad_magic.담음");
    {
        std::ofstream bad(std::filesystem::u8path(path), std::ios::binary);
        bad.write("XXXX", 4);
        for (int i = 0; i < 12; ++i) bad.put(0);  // padding
    }
    REQUIRE_THROWS_AS(read(path), SeumError);
}

TEST_CASE("담음: 한글 파일명 round-trip (Windows 안전)", "[dameum]") {
    Container in;
    Chunk ch;
    ch.type = ChunkType::Source;
    ch.data = {'세' & 0xFF, '움' & 0xFF};  // 의미 없는 byte, path 위주 테스트
    ch.uncompressed_size = static_cast<std::uint32_t>(ch.data.size());
    in.chunks.push_back(std::move(ch));

    auto path = temp_path("세움_테스트.담음");
    write(in, path);
    Container out = read(path);

    REQUIRE(out.chunks.size() == 1);
    REQUIRE(out.chunks[0].type == ChunkType::Source);
}

TEST_CASE("담음: disassemble 한글 자모 출력", "[dameum]") {
    Container c;
    Chunk ch;
    ch.type = ChunkType::Code;
    ch.data = {0x00};
    ch.uncompressed_size = 1;
    c.chunks.push_back(std::move(ch));

    auto out = disassemble(c);
    REQUIRE(out.find(U"담음") != std::u32string::npos);
    REQUIRE(out.find(U"ㄱ")   != std::u32string::npos);
    REQUIRE(out.find(U"코드") != std::u32string::npos);
}

TEST_CASE("담음: 모든 chunk body 가 16-byte aligned", "[dameum]") {
    Container in;
    Chunk a; a.type = ChunkType::Code; a.data.assign(3, 0xAA); a.uncompressed_size = 3;
    Chunk b; b.type = ChunkType::Name; b.data.assign(17, 0xBB); b.uncompressed_size = 17;
    in.chunks.push_back(std::move(a));
    in.chunks.push_back(std::move(b));

    auto path = temp_path("test_align.담음");
    write(in, path);

    // 직접 파일 읽어서 chunk table 의 offset 들이 16의 배수인지 검증
    std::ifstream fi(std::filesystem::u8path(path), std::ios::binary);
    std::ostringstream ss; ss << fi.rdbuf();
    std::string s = ss.str();
    auto rd_u32 = [&](std::size_t at) -> std::uint32_t {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data()) + at;
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
             | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    };
    std::uint32_t off0 = rd_u32(16 + 4);   // 첫 chunk entry 의 offset
    std::uint32_t off1 = rd_u32(16 + 16 + 4);
    REQUIRE(off0 % ALIGNMENT == 0);
    REQUIRE(off1 % ALIGNMENT == 0);
}
