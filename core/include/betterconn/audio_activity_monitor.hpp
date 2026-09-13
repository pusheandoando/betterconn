// core/include/betterconn/audio_activity_monitor.hpp
#pragma once

#include <string>
#include <unordered_set>





namespace betterconn {
class AudioActivityMonitor {
public:
    static bool available();
    static std::unordered_set<int> pids_playing_audio();

private:
    static bool pactl_available();
    static std::unordered_set<int> parse_sink_inputs(const std::string& output);
    static std::string run_and_capture(const std::string& cmd);
};
}