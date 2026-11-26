#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <utility>

struct VarState {
    std::string name;
    std::string type;
    std::string value;
    uintptr_t address;
    uintptr_t points_to;
};

struct ExecutionStep {
    int line;
    std::string func;
    std::vector<VarState> vars;
};

class TracerRuntime {
public:
    static inline std::vector<ExecutionStep> steps;
    static inline std::vector<std::string> call_stack;

    static void enter_func(const std::string& name, int line) {
        call_stack.push_back(name);
    }

    static void exit_func() {
        if (!call_stack.empty()) call_stack.pop_back();
    }

    static void record_step(int line, const std::vector<VarState>& vars) {
        if (steps.size() >= 1000) return;
        std::string current_func = call_stack.empty() ? "global" : call_stack.back();
        steps.push_back({line, current_func, vars});
    }

    template <typename T>
    static VarState make_var(const std::string& name, const T& val, const void* addr) {
        std::ostringstream oss;
        oss << val;
        return {name, "var", oss.str(), reinterpret_cast<uintptr_t>(addr), 0};
    }

    template <typename T>
    static VarState make_ptr(const std::string& name, T* ptr, const void* addr) {
        std::ostringstream oss;
        oss << reinterpret_cast<uintptr_t>(ptr);
        return {name, "ptr", oss.str(), reinterpret_cast<uintptr_t>(addr), reinterpret_cast<uintptr_t>(ptr)};
    }

    template <typename T>
    static std::vector<VarState> make_arr(const std::string& name, const T* arr, size_t size) {
        std::vector<VarState> res;
        for (size_t i = 0; i < size; ++i) {
            res.push_back({
                name + "[" + std::to_string(i) + "]",
                "arr_elem",
                std::to_string(arr[i]),
                reinterpret_cast<uintptr_t>(&arr[i]),
                0
            });
        }
        return res;
    }

    static std::string to_json() {
        std::ostringstream out;
        out << "[\n";
        for (size_t i = 0; i < steps.size(); ++i) {
            out << "  {\"line\": " << steps[i].line << ", \"func\": \"" << steps[i].func << "\", \"vars\": [";
            for (size_t j = 0; j < steps[i].vars.size(); ++j) {
                const auto& v = steps[i].vars[j];
                out << "{\"name\":\"" << v.name << "\",\"type\":\"" << v.type
                    << "\",\"value\":\"" << v.value << "\",\"address\":" << v.address
                    << ",\"points_to\":" << v.points_to << "}";
                if (j + 1 < steps[i].vars.size()) out << ",";
            }
            out << "]}";
            if (i + 1 < steps.size()) out << ",";
            out << "\n";
        }
        out << "]";
        return out.str();
    }
};

#define __trace_enter_func(name, line) TracerRuntime::enter_func(name, line)
#define __trace_exit_func() TracerRuntime::exit_func()

inline void append_vars(std::vector<VarState>& target, const VarState& single) {
    target.push_back(single);
}
inline void append_vars(std::vector<VarState>& target, const std::vector<VarState>& list) {
    target.insert(target.end(), list.begin(), list.end());
}

template <typename... Args>
inline void __trace_step(int line, Args&&... args) {
    std::vector<VarState> all_vars;
    (append_vars(all_vars, std::forward<Args>(args)), ...);
    TracerRuntime::record_step(line, all_vars);
}

#define TRACE_VAR(x) TracerRuntime::make_var(#x, x, &x)
#define TRACE_PTR(p) TracerRuntime::make_ptr(#p, p, &p)
#define TRACE_ARR(a, n) TracerRuntime::make_arr(#a, a, n)
