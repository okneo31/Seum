#include "dameum.h"

#include "error_reporter.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace seum::dameum {

namespace {

// --- byte 직렬화 helpers (little-endian) ---

void put_u8 (std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

// --- CRC32 (Ethernet polynomial, IEEE 802.3) ---
// 자체 구현 (잠긴 결정 #13 외부 의존 0).
std::uint32_t crc32_table[256];
bool crc32_table_init = false;

void init_crc32_table() {
    if (crc32_table_init) return;
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_init = true;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    init_crc32_table();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// 16-byte 경계로 padding 채움 (0x00). 명세 §4.3.
void pad_to_alignment(std::vector<std::uint8_t>& out) {
    while (out.size() % ALIGNMENT != 0) out.push_back(0);
}

[[noreturn]] void raise_io(const std::string& msg) {
    // UTF-8 직접 — std::u32string(begin,end) 는 signed char sign-extension 으로 깨짐.
    raise(Position{}, std::string("담음 입출력 오류: ") + msg);
}

}  // namespace

char32_t chunk_type_jamo(ChunkType t) {
    switch (t) {
        case ChunkType::Code:       return U'ㄱ';
        case ChunkType::Name:       return U'ㄴ';
        case ChunkType::Meta:       return U'ㄷ';
        case ChunkType::State:      return U'ㅁ';
        case ChunkType::Media:      return U'ㅂ';
        case ChunkType::Source:     return U'ㅅ';
        case ChunkType::Link:       return U'ㅇ';
        case ChunkType::KoreanText: return U'ㅎ';
    }
    return U'?';
}

const char32_t* chunk_type_name(ChunkType t) {
    switch (t) {
        case ChunkType::Code:       return U"코드";
        case ChunkType::Name:       return U"이름";
        case ChunkType::Meta:       return U"메타";
        case ChunkType::State:      return U"상태";
        case ChunkType::Media:      return U"미디어";
        case ChunkType::Source:     return U"소스";
        case ChunkType::Link:       return U"링크";
        case ChunkType::KoreanText: return U"한글텍스트";
    }
    return U"???";
}

void write(const Container& c, const std::string& utf8_path) {
    // 1) chunk body 의 file offset 계산 (16-byte alignment).
    const std::uint32_t chunk_count = static_cast<std::uint32_t>(c.chunks.size());
    const std::uint32_t table_size  = chunk_count * CHUNK_TABLE_ENTRY_SIZE;
    std::uint32_t cursor = HEADER_SIZE + table_size;
    while (cursor % ALIGNMENT != 0) ++cursor;  // table 끝 padding

    std::vector<std::uint32_t> body_offsets(chunk_count);
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        body_offsets[i] = cursor;
        cursor += static_cast<std::uint32_t>(c.chunks[i].data.size());
        while (cursor % ALIGNMENT != 0) ++cursor;
    }
    const std::uint32_t total_size = cursor;

    // 2) 버퍼 마련.
    std::vector<std::uint8_t> buf;
    buf.reserve(total_size);

    // 3) 헤더 first 12 bytes (CRC 전).
    buf.push_back('S'); buf.push_back('E'); buf.push_back('U'); buf.push_back('M');
    put_u16(buf, c.version);
    put_u16(buf, c.flags);
    put_u32(buf, chunk_count);
    // 4) CRC32 계산 (first 12 bytes).
    std::uint32_t header_crc = crc32(buf.data(), buf.size());
    put_u32(buf, header_crc);

    // 5) Chunk table entries.
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const Chunk& ch = c.chunks[i];
        put_u8 (buf, static_cast<std::uint8_t>(ch.type));
        put_u8 (buf, static_cast<std::uint8_t>(ch.compression));
        put_u16(buf, ch.flags);
        put_u32(buf, body_offsets[i]);
        put_u32(buf, static_cast<std::uint32_t>(ch.data.size()));
        put_u32(buf, ch.uncompressed_size > 0 ? ch.uncompressed_size
                                              : static_cast<std::uint32_t>(ch.data.size()));
    }
    pad_to_alignment(buf);

    // 6) Chunk bodies.
    for (const Chunk& ch : c.chunks) {
        buf.insert(buf.end(), ch.data.begin(), ch.data.end());
        pad_to_alignment(buf);
    }

    // 7) Disk write (Windows 한글 path 안전 — Windows pitfall #5).
    std::ofstream out(std::filesystem::u8path(utf8_path), std::ios::binary);
    if (!out) raise_io("쓰기 실패: " + utf8_path);
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    if (!out) raise_io("쓰기 중 오류: " + utf8_path);
}

Container read(const std::string& utf8_path) {
    std::ifstream in(std::filesystem::u8path(utf8_path), std::ios::binary);
    if (!in) raise_io("열기 실패: " + utf8_path);

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(s.data());
    const std::size_t   size = s.size();

    if (size < HEADER_SIZE) raise_io("헤더보다 작음: " + utf8_path);
    if (data[0] != 'S' || data[1] != 'E' || data[2] != 'U' || data[3] != 'M') {
        raise_io("magic 불일치 (SEUM 필요): " + utf8_path);
    }
    std::uint16_t version     = read_u16(data + 4);
    std::uint16_t flags       = read_u16(data + 6);
    std::uint32_t chunk_count = read_u32(data + 8);
    std::uint32_t header_crc  = read_u32(data + 12);
    std::uint32_t computed_crc = crc32(data, 12);
    if (header_crc != computed_crc) raise_io("헤더 CRC 불일치: " + utf8_path);
    if (version != DAMEUM_VERSION_V03) {
        raise_io("지원 안 되는 담음 버전: " + utf8_path);
    }

    // Chunk table 시작 = 16.
    std::uint32_t table_size = chunk_count * CHUNK_TABLE_ENTRY_SIZE;
    if (HEADER_SIZE + table_size > size) raise_io("chunk table 초과: " + utf8_path);

    Container c;
    c.version = version;
    c.flags   = flags;
    c.chunks.reserve(chunk_count);

    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const std::uint8_t* e = data + HEADER_SIZE + i * CHUNK_TABLE_ENTRY_SIZE;
        Chunk ch;
        ch.type              = static_cast<ChunkType>(e[0]);
        ch.compression       = static_cast<Compression>(e[1]);
        ch.flags             = read_u16(e + 2);
        std::uint32_t offset = read_u32(e + 4);
        std::uint32_t cs     = read_u32(e + 8);
        ch.uncompressed_size = read_u32(e + 12);

        if (offset + cs > size) {
            raise_io("chunk body 범위 초과: " + utf8_path);
        }
        if (offset % ALIGNMENT != 0) {
            raise_io("chunk alignment 위반: " + utf8_path);
        }
        ch.data.assign(data + offset, data + offset + cs);
        c.chunks.push_back(std::move(ch));
    }
    return c;
}

std::u32string disassemble(const Container& c) {
    auto u32_int = [](std::int64_t v) {
        std::string s = std::to_string(v);
        return std::u32string(s.begin(), s.end());
    };

    std::u32string out;
    out += U"=== 담음 v";
    out += u32_int(c.version);
    out += U" flags=";
    out += u32_int(c.flags);
    out += U" chunks=";
    out += u32_int(static_cast<std::int64_t>(c.chunks.size()));
    out += U" ===\n";

    for (std::size_t i = 0; i < c.chunks.size(); ++i) {
        const Chunk& ch = c.chunks[i];
        out += U"  [";
        out += u32_int(static_cast<std::int64_t>(i));
        out += U"] ";
        out.push_back(chunk_type_jamo(ch.type));
        out += U" ";
        out += chunk_type_name(ch.type);
        out += U" — ";
        switch (ch.compression) {
            case Compression::Raw:         out += U"raw"; break;
            case Compression::Zlib:        out += U"zlib"; break;
            case Compression::JamoHuffman: out += U"자모"; break;
        }
        out += U" ";
        out += u32_int(static_cast<std::int64_t>(ch.data.size()));
        out += U"B (원본 ";
        out += u32_int(static_cast<std::int64_t>(ch.uncompressed_size));
        out += U"B)\n";
    }
    return out;
}

}  // namespace seum::dameum
