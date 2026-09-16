// core/include/betterconn/window_focus_backend.hpp
#pragma once

#include <string>





namespace betterconn {
enum class WindowFocusBackend {
    None,
    X11Xdotool,
    WaylandSway,
    WaylandHyprland,
};


class WindowFocusDetector {
public:
    static WindowFocusBackend detect_available_backend(const std::string& env_prefix);
    static int get_focused_pid(WindowFocusBackend backend, const std::string& env_prefix);
    static int get_pid_under_cursor(WindowFocusBackend backend, const std::string& env_prefix);

private:
    static bool xdotool_available();
    static bool swaymsg_available(const std::string& env_prefix);
    static bool hyprctl_available(const std::string& env_prefix);

    static int get_focused_pid_x11(const std::string& env_prefix);
    static int get_focused_pid_sway(const std::string& env_prefix);
    static int get_focused_pid_hyprland(const std::string& env_prefix);

    static int get_pid_under_cursor_x11(const std::string& env_prefix);
    static int get_pid_under_cursor_hyprland(const std::string& env_prefix);

    static std::string run_and_capture(const std::string& command);
    static int run_and_capture_pid(const std::string& command, const char* anchor_key);
    static bool parse_shell_field(const std::string& output, const char* key, long& out_value);
};
}