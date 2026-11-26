#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <utility>
#include <nlohmann/json.hpp>
#include <xeus/xinterpreter.hpp>

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

    static void clear() {
        steps.clear();
        call_stack.clear();
    }

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
        oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(ptr);
        return {name, "ptr", oss.str(), reinterpret_cast<uintptr_t>(addr), reinterpret_cast<uintptr_t>(ptr)};
    }

    template <typename T>
    static std::vector<VarState> make_arr(const std::string& name, const T* arr, size_t size) {
        std::vector<VarState> res;
        for (size_t i = 0; i < size; ++i) {
            std::ostringstream oss;
            oss << arr[i];
            res.push_back({
                name + "[" + std::to_string(i) + "]",
                "arr_elem",
                oss.str(),
                reinterpret_cast<uintptr_t>(&arr[i]),
                0
            });
        }
        return res;
    }

    static std::string to_json() {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < steps.size(); ++i) {
            out << "{\"line\":" << steps[i].line << ",\"func\":\"" << steps[i].func << "\",\"vars\":[";
            for (size_t j = 0; j < steps[i].vars.size(); ++j) {
                const auto& v = steps[i].vars[j];
                out << "{\"name\":\"" << v.name << "\",\"type\":\"" << v.type
                    << "\",\"value\":\"" << v.value << "\",\"address\":" << v.address
                    << ",\"points_to\":" << v.points_to << "}";
                if (j + 1 < steps[i].vars.size()) out << ",";
            }
            out << "]}";
            if (i + 1 < steps.size()) out << ",";
        }
        out << "]";
        return out.str();
    }

    static std::string render_html() {
        std::string uid = "memviz_" + std::to_string(reinterpret_cast<uintptr_t>(&steps)) + "_" + std::to_string(steps.size());
        std::string json_data = to_json();

        std::ostringstream html;
        html << R"HTML(
<div id=")HTML" << uid << R"HTML(" style="font-family: 'Segoe UI', system-ui, sans-serif; border: 1px solid #cbd5e1; border-radius: 8px; padding: 14px; background: #f8fafc; color: #0f172a; margin-top: 8px;">
  <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; flex-wrap: wrap; gap: 8px;">
    <div style="font-weight: 700; font-size: 15px; color: #0f766e;">🧠 Визуализация на паметта (C++ Stack & Pointers)</div>
    <div style="display: flex; align-items: center; gap: 6px;">
      <button class="mv-prev" style="padding: 4px 10px; border: 1px solid #94a3b8; border-radius: 4px; background: #fff; cursor: pointer;">◀ Назад</button>
      <span class="mv-step-label" style="font-size: 13px; font-weight: 600; min-width: 110px; text-align: center;">Стъпка 1 / 1</span>
      <button class="mv-next" style="padding: 4px 10px; border: 1px solid #0d9488; border-radius: 4px; background: #0d9488; color: #fff; cursor: pointer;">Напред ▶</button>
    </div>
  </div>
  <div class="mv-info" style="font-size: 12px; color: #475569; margin-bottom: 10px; background: #e2e8f0; padding: 5px 10px; border-radius: 4px;"></div>
  <div class="mv-cards" style="display: flex; flex-wrap: wrap; gap: 10px;"></div>
  <script>
    (function() {
      const root = document.getElementById(")HTML" << uid << R"HTML(");
      const steps = )HTML" << json_data << R"HTML(;
      let idx = 0;

      const prevBtn = root.querySelector(".mv-prev");
      const nextBtn = root.querySelector(".mv-next");
      const stepLabel = root.querySelector(".mv-step-label");
      const infoDiv = root.querySelector(".mv-info");
      const cardsDiv = root.querySelector(".mv-cards");

      function render() {
        if (!steps || steps.length === 0) {
          stepLabel.textContent = "Няма записани стъпки";
          infoDiv.textContent = "Няма проследени променливи в тази клетка.";
          cardsDiv.innerHTML = "";
          return;
        }
        const s = steps[idx];
        const funcLabel = (s.func === "__student_cell__" || s.func === "global") ? "main (главна програма)" : s.func;
        stepLabel.textContent = `Стъпка ${idx + 1} от ${steps.length}`;
        infoDiv.innerHTML = `Функция: <b>${funcLabel}</b> | Ред: <b>${s.line}</b>`;
        prevBtn.disabled = idx === 0;
        nextBtn.disabled = idx === steps.length - 1;

        const addrMap = {};
        s.vars.forEach(v => { addrMap[v.address] = v.name; });

        cardsDiv.innerHTML = s.vars.map(v => {
          const hexAddr = "0x" + v.address.toString(16);
          const isPtr = v.type === "ptr";
          const isArr = v.type === "arr_elem";
          const borderCol = isPtr ? "#7c3aed" : (isArr ? "#0284c7" : "#0d9488");
          const badgeBg = isPtr ? "#ede9fe" : (isArr ? "#e0f2fe" : "#ccfbf1");
          const badgeTxt = isPtr ? "POINTER" : (isArr ? "ARRAY" : "VAR");
          const targetName = (isPtr && v.points_to && addrMap[v.points_to]) ? ` → сочи към <b>${addrMap[v.points_to]}</b>` : "";

          return `
            <div style="border: 2px solid ${borderCol}; border-radius: 6px; background: #ffffff; padding: 8px 12px; min-width: 135px; box-shadow: 0 1px 2px rgba(0,0,0,0.05);">
              <div style="display: flex; justify-content: space-between; align-items: center; gap: 6px; margin-bottom: 4px;">
                <span style="font-weight: 700; font-family: monospace; font-size: 14px;">${v.name}</span>
                <span style="font-size: 10px; font-weight: 700; padding: 1px 5px; border-radius: 3px; background: ${badgeBg}; color: ${borderCol};">${badgeTxt}</span>
              </div>
              <div style="font-size: 18px; font-weight: 700; font-family: monospace; color: #1e293b; margin: 4px 0;">${v.value}</div>
              <div style="font-size: 11px; font-family: monospace; color: #64748b;">Адрес: ${hexAddr}${targetName}</div>
            </div>
          `;
        }).join("");
      }

      prevBtn.onclick = () => { if (idx > 0) { idx--; render(); } };
      nextBtn.onclick = () => { if (idx < steps.length - 1) { idx++; render(); } };
      render();
    })();
  </script>
</div>
)HTML";
        return html.str();
    }
};

inline void show_memory_widget() {
    nlohmann::json pub_data;
    pub_data["text/html"] = TracerRuntime::render_html();
    xeus::get_interpreter().display_data(
        std::move(pub_data),
        nlohmann::json::object(),
        nlohmann::json::object()
    );
}

#define SHOW_MEMORY() show_memory_widget()

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
