#include "environment.h"

namespace seum {

void Environment::define(const std::u32string& name, Value v) {
    bindings_[name] = std::move(v);
}

std::optional<Value> Environment::get(const std::u32string& name) const {
    auto it = bindings_.find(name);
    if (it == bindings_.end()) {
        if (parent_) return parent_->get(name);
        return std::nullopt;
    }
    const Value& v = it->second;
    if (const GetterValue* getter = as_getter(v)) {
        return getter->fn();
    }
    return v;
}

void Environment::add_module(const std::u32string& name, ModuleInitializer init) {
    modules_[name] = std::move(init);
}

void Environment::set_dameum_resolver(DameumResolver r) {
    dameum_resolver_ = std::move(r);
}

bool Environment::import_module(const std::u32string& name) {
    auto it = modules_.find(name);
    if (it != modules_.end()) {
        if (imported_.count(name)) return true;
        it->second(*this);
        imported_.insert(name);
        return true;
    }
    // v0.3e: .담음 fallback.
    if (dameum_resolver_ && dameum_resolver_(name, *this)) {
        imported_.insert(name);
        return true;
    }
    return false;
}

}  // namespace seum
