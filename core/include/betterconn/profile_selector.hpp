// core/include/betterconn/profile_selector.hpp
#pragma once

#include <string>





namespace betterconn {
enum class LatencyProfile {
    Latency,
    Balanced,
    Throughput,
};


class ProfileSelector {
public:
    static LatencyProfile select(const std::string& iface);
    static std::string to_string(LatencyProfile profile);

private:
    static bool is_wifi(const std::string& iface);
    static int read_link_speed_mbps(const std::string& iface);
};
}