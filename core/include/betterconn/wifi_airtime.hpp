// core/include/betterconn/wifi_airtime.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
struct AirtimeQueueLimit {
    int access_category;
    int low_us;
    int high_us;
};


class WifiAirtime {
public:
    static void apply(const std::string& iface);
    static void revert();

private:
    static std::vector<AirtimeQueueLimit> tightened_limits();
    static std::string phy_name_for(const std::string& iface);
    static std::string phy_debugfs_path(const std::string& phy);
    static bool debugfs_available();
    static std::vector<AirtimeQueueLimit> read_queue_limits(const std::string& phy_path);
    static void write_queue_limit(const std::string& phy_path, const AirtimeQueueLimit& limit);
    static std::string serialize_limits(const std::vector<AirtimeQueueLimit>& limits);
    static std::vector<AirtimeQueueLimit> deserialize_limits(const std::string& raw);
    static std::string read_first_line(const std::string& path);
    static bool write_line(const std::string& path, const std::string& value);
};
}