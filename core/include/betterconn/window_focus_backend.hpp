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
    static WindowFocusBackend detect_available_backend();
    static int get_focused_pid(WindowFocusBackend backend);
    static int get_pid_under_cursor(WindowFocusBackend backend);

private:
    static bool xdotool_available();
    static bool swaymsg_available();
    static bool hyprctl_available();

    static int get_focused_pid_x11();
    static int get_focused_pid_sway();
    static int get_focused_pid_hyprland();

    static int get_pid_under_cursor_x11();
    static int get_pid_under_cursor_hyprland();

    static int run_and_capture_pid(const char* command, const char* anchor_key);
    static bool parse_shell_field(const std::string& output, const char* key, long& out_value);
};
}