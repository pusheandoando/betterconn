// core/src/session_environment.cpp
#include "betterconn/session_environment.hpp"

#include <cctype>
#include <vector>
#include <fstream>
#include <filesystem>





namespace betterconn {
namespace {
bool looks_like_pid_directory(const std::string& name) {
    if (name.empty()) return false;

    for (char c : name) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }

    return true;
}


std::vector<std::string> read_environ_entries(const std::string& path) {
    std::vector<std::string> entries;
    std::ifstream f(path, std::ios::binary);

    if (!f) return entries;

    std::string entry;
    char c;

    while (f.get(c)) {
        if (c == '\0') {
            if (!entry.empty()) entries.push_back(entry);

            entry.clear();
            continue;
        }

        entry += c;
    }

    if (!entry.empty()) entries.push_back(entry);

    return entries;
}


std::string value_for(const std::vector<std::string>& entries, const std::string& key) {
    std::string prefix = key + "=";

    for (const auto& entry : entries) {
        if (entry.rfind(prefix, 0) == 0) return entry.substr(prefix.size());
    }

    return "";
}


bool session_is_usable(const GraphicalSession& session) {
    if (!session.display.empty() && !session.xauthority.empty()) return true;
    if (!session.wayland_display.empty() && !session.xdg_runtime_dir.empty()) return true;

    return false;
}


void append_shell_quoted(std::string& out, const std::string& value) {
    out += "'";

    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }

    out += "'";
}


void append_assignment(std::string& out, const char* key, const std::string& value) {
    if (value.empty()) return;

    out += key;
    out += "=";
    append_shell_quoted(out, value);
    out += " ";
}
}





GraphicalSession SessionEnvironment::detect() {
    GraphicalSession best;

    std::error_code ec;
    std::filesystem::directory_iterator it("/proc", ec);

    if (ec) return best;

    std::filesystem::directory_iterator end;

    while (it != end) {
        std::string name = it->path().filename().string();
        std::string environ_path = it->path().string() + "/environ";

        it.increment(ec);

        if (ec) break;
        if (!looks_like_pid_directory(name)) continue;

        auto variables = read_environ_entries(environ_path);
        if (variables.empty()) continue;

        GraphicalSession candidate;
        candidate.display = value_for(variables, "DISPLAY");
        candidate.xauthority = value_for(variables, "XAUTHORITY");
        candidate.wayland_display = value_for(variables, "WAYLAND_DISPLAY");
        candidate.xdg_runtime_dir = value_for(variables, "XDG_RUNTIME_DIR");
        candidate.sway_sock = value_for(variables, "SWAYSOCK");
        candidate.hyprland_instance_signature = value_for(variables, "HYPRLAND_INSTANCE_SIGNATURE");

        // Most desktop sessions never export XAUTHORITY because the default cookie location is implicit
        if (!candidate.display.empty() && candidate.xauthority.empty()) {
            std::string home = value_for(variables, "HOME");

            if (!home.empty() && std::filesystem::exists(home + "/.Xauthority")) {
                candidate.xauthority = home + "/.Xauthority";
            }
        }

        if (!session_is_usable(candidate)) continue;

        candidate.valid = true;

        // A compositor IPC socket pins down the exact session, so it wins over a bare display match
        if (!candidate.sway_sock.empty() || !candidate.hyprland_instance_signature.empty()) return candidate;

        if (!best.valid) best = candidate;
    }

    return best;
}


std::string SessionEnvironment::command_prefix(const GraphicalSession& session) {
    if (!session.valid) return "";

    std::string prefix;

    append_assignment(prefix, "DISPLAY", session.display);
    append_assignment(prefix, "XAUTHORITY", session.xauthority);
    append_assignment(prefix, "WAYLAND_DISPLAY", session.wayland_display);
    append_assignment(prefix, "XDG_RUNTIME_DIR", session.xdg_runtime_dir);
    append_assignment(prefix, "SWAYSOCK", session.sway_sock);
    append_assignment(prefix, "HYPRLAND_INSTANCE_SIGNATURE", session.hyprland_instance_signature);

    return prefix;
}
}