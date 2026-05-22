#include "native_bridge.h"

#include "../utf8.h"
#include "../value.h"
#include "../error_reporter.h"
#include "time_internal.h"

#include <utility>

namespace seum::modules {

namespace {

Value make_native_show(OutputSink sink) {
    auto fn = std::make_shared<CallableValue>();
    fn->name = U"native_보여주기";
    fn->fn = [sink = std::move(sink)](std::vector<Value>& args, Position) -> Value {
        std::u32string buf;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) buf.push_back(U' ');
            buf += render_value(args[i]);
        }
        buf.push_back(U'\n');
        sink(buf);
        return make_none();
    };
    return Value{fn};
}

Value make_native_now_sst_seconds() {
    auto fn = std::make_shared<CallableValue>();
    fn->name = U"native_시각_시스템_초";
    fn->fn = [](std::vector<Value>& args, Position pos) -> Value {
        if (!args.empty()) raise(pos, U"native_시각_시스템_초 는 인자 0개여야 합니다");
        return make_int(current_sst_seconds());
    };
    return Value{fn};
}

}  // namespace

void register_natives(Environment& env, OutputSink sink) {
    env.define(U"native_보여주기",        make_native_show(std::move(sink)));
    env.define(U"native_시각_시스템_초", make_native_now_sst_seconds());
}

}  // namespace seum::modules
