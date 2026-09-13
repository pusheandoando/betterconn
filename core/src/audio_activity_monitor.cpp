// core/src/audio_activity_monitor.cpp
#include "betterconn/audio_activity_monitor.hpp"

#include <cstdio>
#include <sstream>





namespace betterconn {
std::string AudioActivityMonitor::run_and_capture(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";

    std::string output;
    char buf[512];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    return output;
}


bool AudioActivityMonitor::pactl_available() {
    return system("command -v pactl >/dev/null 2>&1") == 0;
}


bool AudioActivityMonitor::available() {
    return pactl_available();
}


std::unordered_set<int> AudioActivityMonitor::parse_sink_inputs(const std::string& output) {
    std::unordered_set<int> active_pids;

    const std::string entry_marker = "Sink Input #";
    const std::string corked_key = "Corked:";
    const std::string pid_key = "application.process.id = \"";

    size_t search_from = 0;

    while (true) {
        auto entry_start = output.find(entry_marker, search_from);
        if (entry_start == std::string::npos) break;

        auto next_entry_start = output.find(entry_marker, entry_start + entry_marker.size());
        auto entry_end = (next_entry_start == std::string::npos) ? output.size() : next_entry_start;

        std::string entry = output.substr(entry_start, entry_end - entry_start);

        auto corked_pos = entry.find(corked_key);
        bool is_corked = false;

        if (corked_pos != std::string::npos) {
            auto value_start = entry.find_first_not_of(" \t", corked_pos + corked_key.size());
            is_corked = (value_start != std::string::npos && entry.compare(value_start, 3, "yes") == 0);
        }

        if (!is_corked) {
            auto pid_pos = entry.find(pid_key);

            if (pid_pos != std::string::npos) {
                auto digits_start = pid_pos + pid_key.size();
                auto digits_end = entry.find('"', digits_start);

                if (digits_end != std::string::npos) {
                    try {
                        int pid = std::stoi(entry.substr(digits_start, digits_end - digits_start));

                        if (pid > 0) active_pids.insert(pid);
                    } catch (...) {
                    }
                }
            }
        }

        search_from = entry_end;
    }

    return active_pids;
}


std::unordered_set<int> AudioActivityMonitor::pids_playing_audio() {
    if (!pactl_available()) return {};

    std::string output = run_and_capture("pactl list sink-inputs 2>/dev/null");

    return parse_sink_inputs(output);
}
}