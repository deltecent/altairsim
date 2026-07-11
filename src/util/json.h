#pragma once
//
// A small JSON value. Deliberately hand-rolled: MCP speaks JSON-RPC, which uses
// a tiny slice of JSON, and pulling in a dependency (plus its build-system
// story on four platforms) to parse `{"method":...}` would cost more than it
// saves. DESIGN.md 2 keeps the core dependency-free; this is why that is cheap.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace altair {

class Json {
public:
    enum class T { Null, Bool, Num, Str, Arr, Obj };

    Json() = default;
    Json(bool b) : t_(T::Bool), num_(b ? 1 : 0) {}
    Json(double n) : t_(T::Num), num_(n) {}
    Json(int n) : t_(T::Num), num_(n) {}
    Json(long long n) : t_(T::Num), num_((double)n) {}
    Json(const char* s) : t_(T::Str), str_(s) {}
    Json(std::string s) : t_(T::Str), str_(std::move(s)) {}

    static Json arr() {
        Json j;
        j.t_ = T::Arr;
        return j;
    }
    static Json obj() {
        Json j;
        j.t_ = T::Obj;
        return j;
    }

    T type() const { return t_; }
    bool isNull() const { return t_ == T::Null; }
    bool isObj() const { return t_ == T::Obj; }

    // Object access. Reading a missing key yields Null rather than throwing --
    // MCP params are frequently optional and a throw would be noise.
    Json& operator[](const std::string& k) {
        t_ = T::Obj;
        return obj_[k];
    }
    const Json& at(const std::string& k) const {
        static const Json null;
        auto it = obj_.find(k);
        return it == obj_.end() ? null : it->second;
    }
    bool has(const std::string& k) const { return obj_.count(k) != 0; }

    void push(Json v) {
        t_ = T::Arr;
        arr_.push_back(std::move(v));
    }
    const std::vector<Json>& items() const { return arr_; }
    const std::map<std::string, Json>& fields() const { return obj_; }

    std::string str(const std::string& d = "") const { return t_ == T::Str ? str_ : d; }
    double num(double d = 0) const { return t_ == T::Num ? num_ : d; }
    long long integer(long long d = 0) const { return t_ == T::Num ? (long long)num_ : d; }
    bool boolean(bool d = false) const { return t_ == T::Bool ? num_ != 0 : d; }

    std::string dump() const;
    static bool parse(const std::string& text, Json& out, std::string& err);

private:
    T t_ = T::Null;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;
};

} // namespace altair
