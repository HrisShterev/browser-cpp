#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <xeus/xinterpreter.hpp>

void show_array(std::string name, int arr[], int size) {
    std::string html = "<div style='font-family:sans-serif; margin:10px 0;'><b>Масив " + name + ":</b><div style='display:flex; margin-top:6px;'>";
    for (int i = 0; i < size; i++) {
        html += "<div style='border:2px solid #1a7f37; background:#dafbe1; padding:8px 14px; text-align:center; margin-right:-2px;'>"
                "<div style='font-size:11px; color:#57606a;'>[" + std::to_string(i) + "]</div>"
                "<div style='font-size:20px; font-weight:bold; color:#1a7f37;'>" + std::to_string(arr[i]) + "</div>"
                "</div>";
    }
    html += "</div></div>";
    nlohmann::json bundle;
    bundle["text/html"] = html;
    xeus::get_interpreter().display_data(bundle, nlohmann::json::object(), nlohmann::json::object());
}

void show_var(std::string name, std::string type, int val, void* addr) {
    nlohmann::json bundle;
    std::stringstream ss;
    ss << "0x" << std::hex << (uintptr_t)addr;
    
    bundle["text/html"] = 
        "<div style='display:inline-block; border:2px solid #0969da; border-radius:8px; padding:10px 16px; margin:6px; background:#f0f7ff; font-family:sans-serif;'>"
        "<div style='font-size:11px; color:#57606a;'>" + type + " | адрес: " + ss.str() + "</div>"
        "<div style='font-size:14px; font-weight:bold; color:#1f2328;'>" + name + "</div>"
        "<div style='font-size:22px; font-weight:bold; color:#0969da; margin-top:4px;'>" + std::to_string(val) + "</div>"
        "</div>";
    xeus::get_interpreter().display_data(bundle, nlohmann::json::object(), nlohmann::json::object());
}
