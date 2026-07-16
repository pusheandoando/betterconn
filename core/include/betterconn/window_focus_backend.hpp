// core/include/betterconn/window_focus_backend.hpp
#pragma once





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

private:
    static bool xdotool_available();
    static bool swaymsg_available();
    static bool hyprctl_available();

    static int get_focused_pid_x11();
    static int get_focused_pid_sway();
    static int get_focused_pid_hyprland();

    static int run_and_capture_pid(const char* command, const char* anchor_key);
};
}