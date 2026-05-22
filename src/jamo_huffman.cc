#include "jamo_huffman.h"

#include "error_reporter.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace seum::jamo {

namespace {

// Unicode Hangul Syllables: U+AC00..U+D7A3
constexpr char32_t HANGUL_BASE = 0xAC00;
constexpr char32_t HANGUL_END  = 0xD7A3;

// 자모 표 — 분해 / 결합용.
// 초성 19개, 중성 21개, 종성 28개 (첫 = 없음).
const std::u32string LEAD  = U"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
const std::u32string VOWEL = U"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";
// TAIL[0] = 없음 (placeholder). 실제로는 이 index 일 때 종성 emit 안 함.
const std::u32string TAIL  = U" ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ";

bool is_hangul_syllable(char32_t c) {
    return c >= HANGUL_BASE && c <= HANGUL_END;
}

std::size_t find_vowel(char32_t c) { return VOWEL.find(c); }
std::size_t find_lead(char32_t c)  { return LEAD.find(c); }
std::size_t find_tail(char32_t c)  { return TAIL.find(c); }

}  // namespace

std::u32string decompose(const std::u32string& text) {
    std::u32string out;
    out.reserve(text.size() * 3);
    for (char32_t c : text) {
        if (is_hangul_syllable(c)) {
            std::uint32_t idx = static_cast<std::uint32_t>(c - HANGUL_BASE);
            std::uint32_t t = idx % 28;
            std::uint32_t v = (idx / 28) % 21;
            std::uint32_t l = idx / 588;
            out.push_back(LEAD[l]);
            out.push_back(VOWEL[v]);
            if (t > 0) out.push_back(TAIL[t]);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string compose(const std::u32string& jamo_seq) {
    std::u32string out;
    out.reserve(jamo_seq.size());
    std::size_t i = 0;
    while (i < jamo_seq.size()) {
        char32_t c = jamo_seq[i];
        std::size_t lead_idx = find_lead(c);
        if (lead_idx != std::u32string::npos && i + 1 < jamo_seq.size()) {
            std::size_t vowel_idx = find_vowel(jamo_seq[i + 1]);
            if (vowel_idx != std::u32string::npos) {
                std::size_t tail_idx = 0;
                if (i + 2 < jamo_seq.size()) {
                    std::size_t found = find_tail(jamo_seq[i + 2]);
                    if (found != std::u32string::npos && found != 0) {
                        // tail 후보. 단 다음 jamo (i+3) 가 모음이면 *다음 음절 초성* → tail X.
                        bool next_is_vowel = (i + 3 < jamo_seq.size())
                            && find_vowel(jamo_seq[i + 3]) != std::u32string::npos;
                        if (!next_is_vowel) tail_idx = found;
                    }
                }
                char32_t syllable = static_cast<char32_t>(
                    HANGUL_BASE
                    + lead_idx * 588
                    + vowel_idx * 28
                    + tail_idx);
                out.push_back(syllable);
                i += (tail_idx > 0 ? 3 : 2);
                continue;
            }
        }
        // 자모 아닌 codepoint 또는 음절 형태 안 됨 — 그대로
        out.push_back(c);
        ++i;
    }
    return out;
}

// === Huffman compression ===
namespace {

struct Node {
    std::uint64_t freq;
    std::uint32_t symbol;     // leaf only
    std::unique_ptr<Node> left, right;
    bool is_leaf() const { return !left && !right; }
};

struct NodePtrCmp {
    bool operator()(const Node* a, const Node* b) const {
        if (a->freq != b->freq) return a->freq > b->freq;
        return a->symbol > b->symbol;  // tie-break for stability
    }
};

// 트리에서 codes 추출. code = (length, bits LSB-first).
struct Code {
    std::uint32_t length;
    std::uint64_t bits;       // up to 64-bit codes (실제로 alphabet 작아 길이 ~16 이하)
};

void build_codes(const Node* n, std::uint64_t bits, std::uint32_t depth,
                 std::unordered_map<std::uint32_t, Code>& out) {
    if (n->is_leaf()) {
        // 단일 symbol Huffman = 1 bit (degenerate). 그래도 depth 0 일 때 보호.
        out[n->symbol] = {depth == 0 ? 1u : depth, bits};
        return;
    }
    build_codes(n->left.get(),  bits,                            depth + 1, out);
    build_codes(n->right.get(), bits | (1ULL << depth),          depth + 1, out);
}

std::unique_ptr<Node> build_tree(const std::vector<std::pair<std::uint32_t, std::uint64_t>>& freqs) {
    // priority queue of raw Node*. 잎 -> 부모 합성.
    auto cmp = [](Node* a, Node* b) {
        if (a->freq != b->freq) return a->freq > b->freq;
        return a->symbol > b->symbol;
    };
    std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> pq(cmp);

    std::vector<std::unique_ptr<Node>> arena;
    for (auto& [sym, freq] : freqs) {
        auto node = std::make_unique<Node>();
        node->freq = freq;
        node->symbol = sym;
        pq.push(node.get());
        arena.push_back(std::move(node));
    }
    if (pq.empty()) return nullptr;

    std::uint32_t next_internal = 0xFFFFFFFFu;  // internal node symbol 채워주기 (구분용)
    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        auto parent = std::make_unique<Node>();
        parent->freq = a->freq + b->freq;
        parent->symbol = next_internal--;
        // a 와 b 의 unique_ptr 를 arena 에서 찾아서 옮김.
        for (auto& p : arena) {
            if (!p) continue;
            if (p.get() == a) parent->left  = std::move(p);
            else if (p.get() == b) parent->right = std::move(p);
        }
        pq.push(parent.get());
        arena.push_back(std::move(parent));
    }
    // root 는 arena 의 마지막 (또는 단일 leaf 라면 그 leaf).
    Node* root_ptr = pq.top();
    std::unique_ptr<Node> root;
    for (auto& p : arena) {
        if (p && p.get() == root_ptr) {
            root = std::move(p);
            break;
        }
    }
    return root;
}

// --- bit I/O ---
struct BitWriter {
    std::vector<std::uint8_t> bytes;
    std::uint8_t cur = 0;
    std::uint32_t cur_bits = 0;  // 0..7
    void write(std::uint64_t bits, std::uint32_t length) {
        for (std::uint32_t i = 0; i < length; ++i) {
            std::uint8_t bit = static_cast<std::uint8_t>((bits >> i) & 1);
            cur |= (bit << cur_bits);
            ++cur_bits;
            if (cur_bits == 8) {
                bytes.push_back(cur);
                cur = 0;
                cur_bits = 0;
            }
        }
    }
    void flush() {
        if (cur_bits > 0) {
            bytes.push_back(cur);
            cur = 0;
            cur_bits = 0;
        }
    }
};

struct BitReader {
    const std::uint8_t* p;
    const std::uint8_t* end;
    std::uint32_t bit_pos = 0;  // 0..7 within current byte
    bool eof() const { return p >= end; }
    int read_bit() {
        if (p >= end) throw std::runtime_error("자모 압축: 비트 스트림 끝");
        int b = (*p >> bit_pos) & 1;
        ++bit_pos;
        if (bit_pos == 8) { ++p; bit_pos = 0; }
        return b;
    }
};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    return v;
}

}  // namespace

std::vector<std::uint8_t> compress(const std::u32string& text) {
    // 1) 자모 분해 → token sequence (codepoints).
    std::u32string jamo = decompose(text);

    // 2) 빈도 집계.
    std::unordered_map<std::uint32_t, std::uint64_t> freq;
    for (char32_t c : jamo) ++freq[static_cast<std::uint32_t>(c)];

    // 3) 정렬된 (symbol, freq) 리스트.
    std::vector<std::pair<std::uint32_t, std::uint64_t>> sorted_freqs(freq.begin(), freq.end());
    std::sort(sorted_freqs.begin(), sorted_freqs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // 4) Huffman tree + codes.
    std::unordered_map<std::uint32_t, Code> codes;
    auto root = build_tree(sorted_freqs);
    if (root) build_codes(root.get(), 0, 0, codes);

    // 5) 직렬화: header (jamo length + freq table) + body (bits).
    //
    // Layout:
    //   u32 jamo_len            (원본 jamo sequence 길이, decompress 검증용)
    //   u32 symbol_count
    //   [u32 symbol, u64 freq] * symbol_count   (정렬되어 결정적 tree 재구성)
    //   u32 bit_length          (실제 비트 수, 마지막 byte 의 padding 검출용)
    //   bytes...                (packed bits)
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(jamo.size()));
    put_u32(out, static_cast<std::uint32_t>(sorted_freqs.size()));
    for (const auto& [sym, f] : sorted_freqs) {
        put_u32(out, sym);
        put_u64(out, f);
    }

    BitWriter bw;
    for (char32_t c : jamo) {
        auto it = codes.find(static_cast<std::uint32_t>(c));
        if (it == codes.end()) throw std::runtime_error("자모 압축: 미정의 심볼");
        bw.write(it->second.bits, it->second.length);
    }
    std::uint32_t bit_length = static_cast<std::uint32_t>(bw.bytes.size() * 8 + bw.cur_bits);
    bw.flush();

    put_u32(out, bit_length);
    out.insert(out.end(), bw.bytes.begin(), bw.bytes.end());
    return out;
}

std::u32string decompress(const std::vector<std::uint8_t>& compressed) {
    if (compressed.size() < 8) throw std::runtime_error("자모 압축: 너무 짧음");
    const std::uint8_t* p = compressed.data();
    const std::uint8_t* end = p + compressed.size();

    std::uint32_t jamo_len     = read_u32(p); p += 4;
    std::uint32_t symbol_count = read_u32(p); p += 4;

    std::vector<std::pair<std::uint32_t, std::uint64_t>> freqs;
    freqs.reserve(symbol_count);
    for (std::uint32_t i = 0; i < symbol_count; ++i) {
        if (p + 12 > end) throw std::runtime_error("자모 압축: freq table 짧음");
        std::uint32_t sym = read_u32(p); p += 4;
        std::uint64_t f   = read_u64(p); p += 8;
        freqs.emplace_back(sym, f);
    }

    if (p + 4 > end) throw std::runtime_error("자모 압축: bit_length 누락");
    std::uint32_t bit_length = read_u32(p); p += 4;

    // 결정적 tree 재구성 (compress 와 동일 순서로 입력).
    auto root = build_tree(freqs);
    if (!root) {
        if (jamo_len != 0) throw std::runtime_error("자모 압축: 빈 tree 인데 jamo_len > 0");
        return U"";
    }

    BitReader br{p, end, 0};
    std::u32string jamo_seq;
    jamo_seq.reserve(jamo_len);

    std::uint32_t bits_read = 0;
    while (jamo_seq.size() < jamo_len) {
        const Node* n = root.get();
        // 단일 leaf tree (alphabet=1) — bit 안 읽고 바로 leaf.
        while (!n->is_leaf()) {
            if (bits_read >= bit_length) throw std::runtime_error("자모 압축: bit 부족");
            int b = br.read_bit();
            ++bits_read;
            n = b ? n->right.get() : n->left.get();
        }
        jamo_seq.push_back(static_cast<char32_t>(n->symbol));
    }
    return compose(jamo_seq);
}

}  // namespace seum::jamo
