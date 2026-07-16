// core/src/window_focus_backend.cpp
#include "betterconn/window_focus_backend.hpp"

#include <cstdio>
#include <cctype>
#include <string>
#include <cstdlib>





namespace betterconn {
bool WindowFocusDetector::xdotool_available() {
    return system("command -v xdotool >/dev/null 2>&1") == 0;
}

bool WindowFocusDetector::swaymsg_available() {
    if (system("command -v swaymsg >/dev/null 2>&1") != 0) return false;

    return system("swaymsg -t get_version >/dev/null 2>&1") == 0;
}

bool WindowFocusDetector::hyprctl_available() {
    if (system("command -v hyprctl >/dev/null 2>&1") != 0) return false;

    return system("hyprctl version >/dev/null 2>&1") == 0;
}

WindowFocusBackend WindowFocusDetector::detect_available_backend() {
    if (xdotool_available() && get_focused_pid(WindowFocusBackend::X11Xdotool) > 0) {
        return WindowFocusBackend::X11Xdotool;
    }

    if (swaymsg_available()) {
        return WindowFocusBackend::WaylandSway;
    }

    if (hyprctl_available()) {
        return WindowFocusBackend::WaylandHyprland;
    }

    return WindowFocusBackend::None;
}

int WindowFocusDetector::run_and_capture_pid(const char* command, const char* anchor_key) {
    FILE* p = popen(command, "r");
    if (!p) return -1;

    std::string output;
    char buf[512];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    auto pos = output.find(anchor_key);
    if (pos == std::string::npos) return -1;

    pos += std::string(anchor_key).size();

    auto digits_start = output.find_first_not_of(" \t", pos);
    if (digits_start == std::string::npos) return -1;
    if (!std::isdigit(static_cast<unsigned char>(output[digits_start]))) return -1;

    auto digits_end = output.find_first_not_of("0123456789", digits_start);
    std::string digits = output.substr(digits_start, digits_end - digits_start);

    try {
        return std::stoi(digits);
    } catch (...) {
        return -1;
    }
}

int WindowFocusDetector::get_focused_pid_x11() {
    FILE* p = popen("xdotool getactivewindow getwindowpid 2>/dev/null", "r");
    if (!p) return -1;

    char buf[32];
    std::string output;

    if (fgets(buf, sizeof(buf), p)) output = buf;

    pclose(p);

    if (output.empty()) return -1;

    try {
        return std::stoi(output);
    } catch (...) {
        return -1;
    }
}

int WindowFocusDetector::get_focused_pid_sway() {
    FILE* p = popen("swaymsg -r -t get_tree 2>/dev/null", "r");
    if (!p) return -1;

    std::string output;
    char buf[4096];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    const std::string focused_marker = "\"focused\"";
    const std::string pid_key = "\"pid\"";

    size_t search_from = 0;

    while (true) {
        auto focused_pos = output.find(focused_marker, search_from);
        if (focused_pos == std::string::npos) return -1;

        auto after_marker = focused_pos + focused_marker.size();
        auto value_start = output.find_first_not_of(" \t:", after_marker);
        bool is_focused_true = (value_start != std::string::npos && output.compare(value_start, 4, "true") == 0);

        auto next_focused_pos = output.find(focused_marker, after_marker);
        auto node_end = (next_focused_pos == std::string::npos) ? output.size() : next_focused_pos;

        if (is_focused_true) {
            auto pid_pos = output.find(pid_key, focused_pos);
            
            if (pid_pos != std::string::npos && pid_pos < node_end) {
                auto pid_value_start = output.find_first_not_of(" \t:", pid_pos + pid_key.size());
                auto digits_start = output.find_first_of("0123456789", pid_value_start);

                if (digits_start != std::string::npos && digits_start == pid_value_start) {
                    auto digits_end = output.find_first_not_of("0123456789", digits_start);
                    std::string digits = output.substr(digits_start, digits_end - digits_start);

                    try {
                        int pid = std::stoi(digits);
                        if (pid > 0) return pid;
                    } catch (...) {
                    }
                }
            }
        }

        search_from = after_marker;
    }
}

int WindowFocusDetector::get_focused_pid_hyprland() {
    return run_and_capture_pid("hyprctl activewindow -j 2>/dev/null", "\"pid\":");
}

int WindowFocusDetector::get_focused_pid(WindowFocusBackend backend) {
    switch (backend) {
        case WindowFocusBackend::X11Xdotool:
            return get_focused_pid_x11();
        case WindowFocusBackend::WaylandSway:
            return get_focused_pid_sway();
        case WindowFocusBackend::WaylandHyprland:
            return get_focused_pid_hyprland();
        case WindowFocusBackend::None:
        default:
            return -1;
    }
}
}