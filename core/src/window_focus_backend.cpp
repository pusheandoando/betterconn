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

bool WindowFocusDetector::parse_shell_field(const std::string& output, const char* key, long& out_value) {
    auto pos = output.find(key);
    if (pos == std::string::npos) return false;

    pos += std::string(key).size();

    auto digits_start = pos;
    bool negative = false;

    if (digits_start < output.size() && output[digits_start] == '-') {
        negative = true;
        ++digits_start;
    }

    if (digits_start >= output.size() || !std::isdigit(static_cast<unsigned char>(output[digits_start]))) return false;

    auto digits_end = output.find_first_not_of("0123456789", digits_start);
    std::string digits = output.substr(digits_start, digits_end - digits_start);

    try {
        out_value = negative ? -std::stol(digits) : std::stol(digits);
        
        return true;
    } catch (...) {
        return false;
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

int WindowFocusDetector::get_pid_under_cursor_x11() {
    FILE* p = popen("xdotool getmouselocation --shell 2>/dev/null", "r");
    if (!p) return -1;

    std::string output;
    char buf[256];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    long window_id = 0;
    if (!parse_shell_field(output, "WINDOW=", window_id) || window_id <= 0) return -1;

    std::string cmd = "xdotool getwindowpid " + std::to_string(window_id) + " 2>/dev/null";

    return run_and_capture_pid(cmd.c_str(), "");
}

int WindowFocusDetector::get_pid_under_cursor_hyprland() {
    FILE* cursor_pipe = popen("hyprctl cursorpos -j 2>/dev/null", "r");
    if (!cursor_pipe) return -1;

    std::string cursor_output;
    char buf[256];

    while (fgets(buf, sizeof(buf), cursor_pipe)) cursor_output += buf;

    pclose(cursor_pipe);

    long cursor_x = 0;
    long cursor_y = 0;

    if (!parse_shell_field(cursor_output, "\"x\":", cursor_x)) return -1;
    if (!parse_shell_field(cursor_output, "\"y\":", cursor_y)) return -1;

    FILE* clients_pipe = popen("hyprctl clients -j 2>/dev/null", "r");
    if (!clients_pipe) return -1;

    std::string clients_output;

    while (fgets(buf, sizeof(buf), clients_pipe)) clients_output += buf;

    pclose(clients_pipe);

    const std::string at_key = "\"at\":";
    size_t search_from = 0;

    while (true) {
        auto at_pos = clients_output.find(at_key, search_from);
        if (at_pos == std::string::npos) return -1;

        auto array_start = clients_output.find('[', at_pos);
        auto array_end = clients_output.find(']', array_start);
        if (array_start == std::string::npos || array_end == std::string::npos) return -1;

        std::string at_array = clients_output.substr(array_start, array_end - array_start + 1);

        long window_x = 0;
        long window_y = 0;

        if (!parse_shell_field(at_array, "[", window_x)) return -1;

        auto comma_pos = at_array.find(',');
        if (comma_pos == std::string::npos) return -1;

        if (!parse_shell_field(at_array.substr(comma_pos), ",", window_y)) return -1;

        auto size_pos = clients_output.find("\"size\":", array_end);
        auto size_array_start = clients_output.find('[', size_pos);
        auto size_array_end = clients_output.find(']', size_array_start);

        long window_width = 0;
        long window_height = 0;

        if (size_pos != std::string::npos && size_array_start != std::string::npos && size_array_end != std::string::npos) {
            std::string size_array = clients_output.substr(size_array_start, size_array_end - size_array_start + 1);

            parse_shell_field(size_array, "[", window_width);

            auto size_comma_pos = size_array.find(',');
            if (size_comma_pos != std::string::npos) {
                parse_shell_field(size_array.substr(size_comma_pos), ",", window_height);
            }
        }

        bool cursor_inside_rect = cursor_x >= window_x && cursor_x < (window_x + window_width)
            && cursor_y >= window_y && cursor_y < (window_y + window_height);

        if (cursor_inside_rect) {
            auto pid_pos = clients_output.find("\"pid\":", array_end);

            if (pid_pos != std::string::npos) {
                long pid_value = 0;

                if (parse_shell_field(clients_output, "\"pid\":", pid_value) && pid_value > 0) {
                    return static_cast<int>(pid_value);
                }
            }
        }

        search_from = array_end;
    }
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

int WindowFocusDetector::get_pid_under_cursor(WindowFocusBackend backend) {
    switch (backend) {
        case WindowFocusBackend::X11Xdotool:
            return get_pid_under_cursor_x11();
        case WindowFocusBackend::WaylandHyprland:
            return get_pid_under_cursor_hyprland();
        case WindowFocusBackend::WaylandSway:
            return get_focused_pid_sway();
        case WindowFocusBackend::None:
        default:
            return -1;
    }
}
}