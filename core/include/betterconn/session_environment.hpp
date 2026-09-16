// core/include/betterconn/session_environment.hpp
#pragma once

#include <string>





namespace betterconn {
struct GraphicalSession {
    std::string display;
    std::string xauthority;
    std::string wayland_display;
    std::string xdg_runtime_dir;
    std::string sway_sock;
    std::string hyprland_instance_signature;
    bool valid = false;
};


class SessionEnvironment {
public:
    static GraphicalSession detect();
    static std::string command_prefix(const GraphicalSession& session);
};
}