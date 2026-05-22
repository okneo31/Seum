#pragma once

#include "common.h"
#include "value.h"

namespace seum {

class Environment {
public:
    using ModuleInitializer = std::function<void(Environment&)>;
    // v0.3e: .담음 검색 콜백. import_module 시 등록된 ModuleInitializer 없으면 fallback.
    // 성공이면 true (이름이 .담음 으로 발견되어 env 에 등록됨).
    using DameumResolver    = std::function<bool(const std::u32string& name, Environment&)>;

    Environment() = default;
    explicit Environment(const Environment* parent) : parent_(parent) {}

    void define(const std::u32string& name, Value v);
    std::optional<Value> get(const std::u32string& name) const;

    void add_module(const std::u32string& name, ModuleInitializer init);
    void set_dameum_resolver(DameumResolver r);

    bool import_module(const std::u32string& name);

private:
    const Environment* parent_ = nullptr;
    std::unordered_map<std::u32string, Value> bindings_;
    std::unordered_map<std::u32string, ModuleInitializer> modules_;
    std::unordered_set<std::u32string> imported_;
    DameumResolver dameum_resolver_;
};

}  // namespace seum
