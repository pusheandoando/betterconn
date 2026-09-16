// core/src/window_focus_backend.cpp
#include "betterconn/window_focus_backend.hpp"

#include <cstdio>
#include <cctype>
#include <string>
#include <cstdlib>





namespace betterconn {
std::string WindowFocusDetector::run_and_capture(const std::string& command) {
    FILE* p = popen(command.c_str(), "r");
    if (!p) return "";

    std::string output;
    char buf[4096];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    return output;
}


bool WindowFocusDetector::xdotool_available() {
    return system("command -v xdotool >/dev/null 2>&1") == 0;
}


bool WindowFocusDetector::swaymsg_available(const std::string& env_prefix) {
    if (system("command -v swaymsg >/dev/null 2>&1") != 0) return false;

    return system((env_prefix + "swaymsg -t get_version >/dev/null 2>&1").c_str()) == 0;
}


bool WindowFocusDetector::hyprctl_available(const std::string& env_prefix) {
    if (system("command -v hyprctl >/dev/null 2>&1") != 0) return false;

    return system((env_prefix + "hyprctl version >/dev/null 2>&1").c_str()) == 0;
}





WindowFocusBackend WindowFocusDetector::detect_available_backend(const std::string& env_prefix) {
    if (xdotool_available() && get_focused_pid_x11(env_prefix) > 0) {
        return WindowFocusBackend::X11Xdotool;
    }

    if (swaymsg_available(env_prefix)) {
        return WindowFocusBackend::WaylandSway;
    }

    if (hyprctl_available(env_prefix)) {
        return WindowFocusBackend::WaylandHyprland;
    }

    return WindowFocusBackend::None;
}





int WindowFocusDetector::run_and_capture_pid(const std::string& command, const char* anchor_key) {
    std::string output = run_and_capture(command);
    if (output.empty()) return -1;

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

    // Compositor JSON keeps a space between the colon and the value, so the digits are not always adjacent to the key
    auto digits_start = output.find_first_not_of(" \t", pos);
    if (digits_start == std::string::npos) return false;

    bool negative = false;

    if (output[digits_start] == '-') {
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


int WindowFocusDetector::get_focused_pid_x11(const std::string& env_prefix) {
    return run_and_capture_pid(env_prefix + "xdotool getactivewindow getwindowpid 2>/dev/null", "");
}


int WindowFocusDetector::get_focused_pid_sway(const std::string& env_prefix) {
    std::string output = run_and_capture(env_prefix + "swaymsg -r -t get_tree 2>/dev/null");
    if (output.empty()) return -1;

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


int WindowFocusDetector::get_focused_pid_hyprland(const std::string& env_prefix) {
    return run_and_capture_pid(env_prefix + "hyprctl activewindow -j 2>/dev/null", "\"pid\":");
}


int WindowFocusDetector::get_pid_under_cursor_x11(const std::string& env_prefix) {
    std::string output = run_and_capture(env_prefix + "xdotool getmouselocation --shell 2>/dev/null");

    long window_id = 0;
    if (!parse_shell_field(output, "WINDOW=", window_id) || window_id <= 0) return -1;

    return run_and_capture_pid(env_prefix + "xdotool getwindowpid " + std::to_string(window_id) + " 2>/dev/null", "");
}


int WindowFocusDetector::get_pid_under_cursor_hyprland(const std::string& env_prefix) {
    std::string cursor_output = run_and_capture(env_prefix + "hyprctl cursorpos -j 2>/dev/null");

    long cursor_x = 0;
    long cursor_y = 0;

    if (!parse_shell_field(cursor_output, "\"x\":", cursor_x)) return -1;
    if (!parse_shell_field(cursor_output, "\"y\":", cursor_y)) return -1;

    std::string clients_output = run_and_capture(env_prefix + "hyprctl clients -j 2>/dev/null");

    const std::string at_key = "\"at\":";
    size_t search_from = 0;

    while (true) {
        auto at_pos = clients_output.find(at_key, search_from);
        if (at_pos == std::string::npos) return -1;

        auto array_start = clients_output.find('[', at_pos);
        auto array_end = clients_output.find(']', array_start);
        if (array_start == std::string::npos || array_end == std::string::npos) return -1;

        std::string at_array = clients_output.substr(array_start, array_end - array_start + 1);
        search_from = array_end;

        long window_x = 0;
        long window_y = 0;

        if (!parse_shell_field(at_array, "[", window_x)) continue;

        auto comma_pos = at_array.find(',');
        if (comma_pos == std::string::npos) continue;

        if (!parse_shell_field(at_array.substr(comma_pos), ",", window_y)) continue;

        auto size_pos = clients_output.find("\"size\":", array_end);
        if (size_pos == std::string::npos) continue;

        auto size_array_start = clients_output.find('[', size_pos);
        auto size_array_end = clients_output.find(']', size_array_start);
        if (size_array_start == std::string::npos || size_array_end == std::string::npos) continue;

        std::string size_array = clients_output.substr(size_array_start, size_array_end - size_array_start + 1);

        long window_width = 0;
        long window_height = 0;

        if (!parse_shell_field(size_array, "[", window_width)) continue;

        auto size_comma_pos = size_array.find(',');
        if (size_comma_pos == std::string::npos) continue;

        if (!parse_shell_field(size_array.substr(size_comma_pos), ",", window_height)) continue;

        bool cursor_inside_rect = cursor_x >= window_x && cursor_x < (window_x + window_width)
            && cursor_y >= window_y && cursor_y < (window_y + window_height);

        if (!cursor_inside_rect) continue;

        auto pid_pos = clients_output.find("\"pid\":", size_array_end);
        if (pid_pos == std::string::npos) continue;

        long pid_value = 0;

        // The lookup has to start at the matching client, otherwise every call returns the first client of the list
        if (parse_shell_field(clients_output.substr(pid_pos), "\"pid\":", pid_value) && pid_value > 0) {
            return static_cast<int>(pid_value);
        }
    }
}


int WindowFocusDetector::get_focused_pid(WindowFocusBackend backend, const std::string& env_prefix) {
    switch (backend) {
        case WindowFocusBackend::X11Xdotool:
            return get_focused_pid_x11(env_prefix);
        case WindowFocusBackend::WaylandSway:
            return get_focused_pid_sway(env_prefix);
        case WindowFocusBackend::WaylandHyprland:
            return get_focused_pid_hyprland(env_prefix);
        case WindowFocusBackend::None:
        default:
            return -1;
    }
}


int WindowFocusDetector::get_pid_under_cursor(WindowFocusBackend backend, const std::string& env_prefix) {
    switch (backend) {
        case WindowFocusBackend::X11Xdotool:
            return get_pid_under_cursor_x11(env_prefix);
        case WindowFocusBackend::WaylandHyprland:
            return get_pid_under_cursor_hyprland(env_prefix);
        case WindowFocusBackend::WaylandSway:
            return get_focused_pid_sway(env_prefix);
        case WindowFocusBackend::None:
        default:
            return -1;
    }
}
}