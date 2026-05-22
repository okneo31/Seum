#include "bytecode.h"
#include "dameum.h"
#include "environment.h"
#include "error_reporter.h"
#include "interpreter.h"
#include "ir.h"
#include "jamo_huffman.h"
#include "lexer.h"
#include "parser.h"
#include "utf8.h"
#include "vm.h"
#include "modules/builtin.h"
#include "modules/native_bridge.h"
#include "modules/time.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#endif

namespace {

void configure_console_utf8() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::string read_file(const std::string& utf8_path) {
    std::ifstream in(std::filesystem::u8path(utf8_path), std::ios::binary);
    if (!in) {
        throw std::runtime_error("파일을 열 수 없습니다: " + utf8_path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// 파일이 '.담음' (SEUM magic) 인지 첫 4 바이트로 판별. v0.3b.
bool is_dameum_file(const std::string& utf8_path) {
    std::ifstream in(std::filesystem::u8path(utf8_path), std::ios::binary);
    if (!in) return false;
    char m[4] = {0,0,0,0};
    in.read(m, 4);
    return in.gcount() == 4 && m[0]=='S' && m[1]=='E' && m[2]=='U' && m[3]=='M';
}

// === v0.3e: .담음 resolver (잠긴 결정 #49 lib_dir 검색) ===

// 라이프타임: import 된 .담음 program 들이 사용 동안 살아있도록 static cache.
std::vector<std::shared_ptr<seum::bytecode::Program>> g_dameum_progs;
std::string g_exe_path;  // argv[0] — install_dameum_resolver 가 사용

std::vector<std::string> compute_lib_dirs(const std::string& exe_path) {
    namespace fs = std::filesystem;
    std::vector<std::string> dirs;

    // 1) SEUM_LIB_PATH (우선)
    if (const char* p = std::getenv("SEUM_LIB_PATH")) {
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string s(p);
        std::size_t start = 0;
        while (start <= s.size()) {
            auto pos = s.find(sep, start);
            std::string part = (pos == std::string::npos)
                ? s.substr(start)
                : s.substr(start, pos - start);
            if (!part.empty()) dirs.push_back(part);
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
    }
    // 2) 실행파일 옆 lib/
    if (!exe_path.empty()) {
        try {
            fs::path exe = fs::u8path(exe_path);
            fs::path lib = exe.parent_path() / "lib";
            dirs.push_back(lib.u8string());
            // 빌드 트리에서 실행 시: build/Release/seum.exe → ../../lib
            fs::path src_lib = exe.parent_path().parent_path().parent_path() / "lib";
            dirs.push_back(src_lib.u8string());
        } catch (...) {}
    }
    // 3) ~/.seum/lib/
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home) {
        dirs.push_back(std::string(home) + "/.seum/lib");
    }
    return dirs;
}

bool try_load_dameum_grut(const std::u32string& name,
                          seum::Environment& env,
                          const std::vector<std::string>& lib_dirs) {
    namespace fs = std::filesystem;
    std::string utf8_name = seum::utf8::encode(name);
    static const std::string dameum_ext = "\xEB\x8B\xB4\xEC\x9D\x8C"; // 담음

    for (const std::string& dir : lib_dirs) {
        std::string fname = utf8_name + "." + dameum_ext;
        fs::path path = fs::u8path(dir) / fs::u8path(fname);
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) continue;
        try {
            auto c = seum::dameum::read(path.u8string());
            for (const auto& ch : c.chunks) {
                if (ch.type == seum::dameum::ChunkType::Code) {
                    auto prog = std::make_shared<seum::bytecode::Program>(
                        seum::bytecode::deserialize(ch.data));
                    g_dameum_progs.push_back(prog);
                    seum::vm::register_program(*prog, env);
                    return true;
                }
            }
        } catch (...) {
            // 다음 디렉터리
        }
    }
    return false;
}

void install_dameum_resolver(seum::Environment& env, const std::string& exe_path) {
    std::vector<std::string> lib_dirs = compute_lib_dirs(exe_path);
    env.set_dameum_resolver(
        [lib_dirs](const std::u32string& name, seum::Environment& env) -> bool {
            return try_load_dameum_grut(name, env, lib_dirs);
        });
}

// `.세움` 경로의 확장자를 `.담음` 으로 치환. 못 찾으면 ".담음" 추가.
std::string default_output_path(const std::string& source_path) {
    // ".세움" UTF-8 bytes = . EC 84 B8 EC 9B 80 (7 bytes including dot)
    static const std::string seum_ext = "\xEC\x84\xB8\xEC\x9B\x80";  // 세움
    static const std::string dameum_ext = "\xEB\x8B\xB4\xEC\x9D\x8C"; // 담음
    std::size_t dot = source_path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = source_path.substr(dot + 1);
        if (ext == seum_ext || ext == "seum") {
            return source_path.substr(0, dot + 1) + dameum_ext;
        }
    }
    return source_path + "." + dameum_ext;
}

// v0.2e ship: 기본 --vm (잠긴 결정 F5).
enum class ExecMode { Tree, VM };

// 텍스트 .세움 또는 binary .담음 → bytecode::Program 으로 통일.
seum::bytecode::Program load_program(const std::string& path) {
    using namespace seum;
    if (is_dameum_file(path)) {
        // .담음 경로: dameum::read → 코드 chunk 추출 → bytecode::deserialize
        dameum::Container c = dameum::read(path);
        for (const dameum::Chunk& ch : c.chunks) {
            if (ch.type == dameum::ChunkType::Code) {
                return bytecode::deserialize(ch.data);
            }
        }
        throw std::runtime_error("담음 안에 코드 chunk 가 없습니다: " + path);
    }
    // .세움 경로: source → tokenize → parse → IR → bytecode
    std::string raw = read_file(path);
    std::u32string src;
    try { src = utf8::decode(raw); }
    catch (const std::exception& ex) {
        throw std::runtime_error("UTF-8 디코드 실패 (" + path + "): " + ex.what());
    }
    auto tokens  = tokenize(src);
    auto program = parse(tokens);
    auto ir_prog = ir::lower(program);
    return bytecode::compile(ir_prog);
}

int run_file(const std::string& path, ExecMode mode) {
    using namespace seum;

    Environment env;
    modules::register_builtin(env);
    modules::register_time(env);
    modules::register_natives(env);
    install_dameum_resolver(env, g_exe_path);

    try {
        if (mode == ExecMode::Tree) {
            // Tree 경로는 .세움 텍스트만 (v0.3b 한정. v0.4+ .담음 tree 경로 검토).
            if (is_dameum_file(path)) {
                std::cerr << "--tree 경로는 .세움 소스에만 지원됩니다 (v0.3b)." << std::endl;
                return 2;
            }
            std::string raw = read_file(path);
            std::u32string source = utf8::decode(raw);
            auto tokens  = tokenize(source);
            auto program = parse(tokens);
            evaluate(program, env);
        } else {
            // VM 경로: .세움 또는 .담음 둘 다 지원.
            auto bc_prog = load_program(path);
            vm::run(bc_prog, env);
        }
    } catch (const SeumError& err) {
        report(err, path);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "예기치 못한 오류: " << ex.what() << std::endl;
        return 2;
    }
    return 0;
}

// `세움 변환 source target` — v0.3d 신규. 방향 자동 감지.
// source 가 .담음 (SEUM magic) 이면 export, 아니면 import.
int convert_file(const std::string& source_path, const std::string& target_path) {
    using namespace seum;
    try {
        if (is_dameum_file(source_path)) {
            // export: .담음 → .txt
            dameum::Container c = dameum::read(source_path);
            std::u32string text;
            bool found = false;
            for (const dameum::Chunk& ch : c.chunks) {
                if (ch.type == dameum::ChunkType::KoreanText) {
                    text  = jamo::decompress(ch.data);
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "담음 안에 한글텍스트 chunk 가 없습니다: " << source_path << std::endl;
                return 2;
            }
            std::string out_utf8 = utf8::encode(text);
            std::ofstream out(std::filesystem::u8path(target_path), std::ios::binary);
            if (!out) {
                std::cerr << "쓰기 실패: " << target_path << std::endl;
                return 2;
            }
            out.write(out_utf8.data(), static_cast<std::streamsize>(out_utf8.size()));
            std::cout << source_path << " → " << target_path
                      << " (" << out_utf8.size() << "B 텍스트)" << std::endl;
        } else {
            // import: .txt → .담음
            std::string raw_utf8 = read_file(source_path);
            std::u32string text  = utf8::decode(raw_utf8);

            dameum::Container c;
            dameum::Chunk text_ch;
            text_ch.type              = dameum::ChunkType::KoreanText;
            text_ch.compression       = dameum::Compression::JamoHuffman;
            text_ch.data              = jamo::compress(text);
            text_ch.uncompressed_size = static_cast<std::uint32_t>(raw_utf8.size());
            c.chunks.push_back(std::move(text_ch));
            // v0.3d: meta chunk 는 v0.3e+ 백로그. round-trip (acc15) 만 보장.

            dameum::write(c, target_path);
            std::cout << source_path << " → " << target_path
                      << " (" << c.chunks[0].data.size() << "B 압축, 원본 "
                      << raw_utf8.size() << "B)" << std::endl;
        }
    } catch (const SeumError& err) {
        report(err, source_path);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "변환 실패: " << ex.what() << std::endl;
        return 2;
    }
    return 0;
}

// `세움 빌드 hello.세움 [output.담음]` — v0.3b 신규.
int build_file(const std::string& source_path, const std::string& output_path) {
    using namespace seum;
    try {
        std::string raw = read_file(source_path);
        std::u32string src = utf8::decode(raw);
        auto tokens  = tokenize(src);
        auto program = parse(tokens);
        auto ir_prog = ir::lower(program);
        auto bc_prog = bytecode::compile(ir_prog);

        // .담음 Container 만들기 — 코드 chunk 만 (다른 chunks v0.3c+).
        dameum::Container c;
        dameum::Chunk code_chunk;
        code_chunk.type = dameum::ChunkType::Code;
        code_chunk.compression = dameum::Compression::Raw;
        code_chunk.data = bytecode::serialize(bc_prog);
        code_chunk.uncompressed_size = static_cast<std::uint32_t>(code_chunk.data.size());
        c.chunks.push_back(std::move(code_chunk));

        dameum::write(c, output_path);
        std::cout << source_path << " → " << output_path << " (" << c.chunks[0].data.size() << "B)" << std::endl;
    } catch (const SeumError& err) {
        report(err, source_path);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "빌드 실패: " << ex.what() << std::endl;
        return 2;
    }
    return 0;
}

void print_usage() {
    std::cerr <<
        "사용법:\n"
        "  세움 실행 [--vm | --tree] <파일.세움|파일.담음>\n"
        "  세움 빌드 <파일.세움> [출력.담음]\n"
        "  세움 변환 <source> <target>            (방향 자동: .담음↔다른 포맷)\n"
        "  seum run/build/convert (영문 alias)\n"
        "기본: --vm (v0.2e+). --tree 는 .세움 소스만.\n";
}

struct ParsedRest {
    ExecMode    mode  = ExecMode::VM;   // F5: v0.2e ship 시 기본 --vm
    std::string file;
    bool        ok    = false;
};

// 명령(`실행`/`run`) 다음 위치부터의 인자들을 파싱.
// 플래그가 0~여러 번 나올 수 있으나 마지막 플래그가 모드를 결정.
ParsedRest parse_rest(const std::vector<std::string>& args, std::size_t start) {
    ParsedRest out;
    std::size_t i = start;
    while (i < args.size()) {
        const std::string& a = args[i];
        if (a == "--vm")   { out.mode = ExecMode::VM;   ++i; continue; }
        if (a == "--tree") { out.mode = ExecMode::Tree; ++i; continue; }
        break;
    }
    if (i < args.size()) {
        out.file = args[i];
        out.ok   = true;
    }
    return out;
}

int seum_main(const std::vector<std::string>& args) {
    configure_console_utf8();
    if (!args.empty()) g_exe_path = args[0];

    if (args.size() < 2) {
        print_usage();
        return 2;
    }
    const std::string& cmd = args[1];
    // '실행' / '빌드' / '변환' UTF-8 bytes.
    const std::string CMD_RUN_KO     = "\xEC\x8B\xA4\xED\x96\x89";  // 실행
    const std::string CMD_BUILD_KO   = "\xEB\xB9\x8C\xEB\x93\x9C";  // 빌드
    const std::string CMD_CONVERT_KO = "\xEB\xB3\x80\xED\x99\x98";  // 변환

    const bool is_run_cmd     = (cmd == "run"     || cmd == CMD_RUN_KO);
    const bool is_build_cmd   = (cmd == "build"   || cmd == CMD_BUILD_KO);
    const bool is_convert_cmd = (cmd == "convert" || cmd == CMD_CONVERT_KO);

    if (is_build_cmd) {
        if (args.size() < 3) { print_usage(); return 2; }
        const std::string& source = args[2];
        std::string output = (args.size() >= 4) ? args[3] : default_output_path(source);
        return build_file(source, output);
    }
    if (is_convert_cmd) {
        // `세움 변환 source target` — 선택적 -> 토큰 무시.
        std::vector<std::string> rest;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] != "->") rest.push_back(args[i]);
        }
        if (rest.size() != 2) { print_usage(); return 2; }
        return convert_file(rest[0], rest[1]);
    }

    std::size_t rest_start = is_run_cmd ? 2 : 1;
    ParsedRest pr = parse_rest(args, rest_start);
    if (!pr.ok) {
        print_usage();
        return 2;
    }
    return run_file(pr.file, pr.mode);
}

#if defined(_WIN32)
// Windows: wmain 으로 UTF-16 명령행을 받아 UTF-8 로 변환.
// 기존 main(argc, char**) 은 ANSI 코드페이지 (대개 CP949) 로 디코드되어
// 한국어 파일명·인자가 깨질 수 있음 (특히 비표준 시스템 로캘에서).
std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), needed, nullptr, nullptr);
    return out;
}
#endif

}  // namespace

#if defined(_WIN32)

int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wide_to_utf8(wargv[i]));
    }
    return seum_main(args);
}

#else

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i] ? argv[i] : "");
    }
    return seum_main(args);
}

#endif
