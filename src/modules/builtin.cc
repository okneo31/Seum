#include "builtin.h"

#include "../utf8.h"
#include "../value.h"

#include <cstdio>
#include <utility>

namespace seum::modules {

OutputSink default_stdout_sink() {
    return [](const std::u32string& s) {
        std::string bytes = utf8::encode(s);
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fflush(stdout);
    };
}

namespace {

Value make_show(OutputSink sink) {
    auto show_fn = std::make_shared<CallableValue>();
    show_fn->name = U"보여주기";
    show_fn->fn = [sink = std::move(sink)](std::vector<Value>& args, Position) -> Value {
        std::u32string buf;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) buf.push_back(U' ');
            buf += render_value(args[i]);
        }
        buf.push_back(U'\n');
        sink(buf);
        return make_none();
    };
    return Value{show_fn};
}

}  // namespace

void register_builtin(Environment& env, OutputSink sink) {
    env.define(U"보여주기", make_show(std::move(sink)));
}

}  // namespace seum::modules
