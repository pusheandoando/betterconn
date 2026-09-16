// core/src/profile_selector.cpp
#include "betterconn/profile_selector.hpp"

#include <fstream>
#include <filesystem>





namespace betterconn {
bool ProfileSelector::is_wifi(const std::string& iface) {
    return std::filesystem::exists("/sys/class/net/" + iface + "/phy80211");
}


int ProfileSelector::read_link_speed_mbps(const std::string& iface) {
    std::ifstream f("/sys/class/net/" + iface + "/speed");
    if (!f) return -1;

    int speed = -1;
    f >> speed;

    if (f.fail()) return -1;

    return speed;
}


LatencyProfile ProfileSelector::select(const std::string& iface) {
    if (iface.empty()) return LatencyProfile::Balanced;

    // WiFi drivers moderate interrupts in firmware, so a forced coalescing profile is ignored at best
    if (is_wifi(iface)) return LatencyProfile::Balanced;

    int speed = read_link_speed_mbps(iface);

    // Past gigabit, minimal coalescing turns into an interrupt storm that costs more latency than it saves
    if (speed > 0 && speed <= 1000) return LatencyProfile::Latency;

    return LatencyProfile::Balanced;
}


std::string ProfileSelector::to_string(LatencyProfile profile) {
    switch (profile) {
        case LatencyProfile::Latency: return "latency";
        case LatencyProfile::Throughput: return "throughput";
        case LatencyProfile::Balanced:
        default: return "balanced";
    }
}
}