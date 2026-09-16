// core/src/wifi_airtime.cpp
#include "betterconn/storage.hpp"
#include "betterconn/wifi_airtime.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>





namespace betterconn {
static constexpr int kTightenedThresholdUs = 8000;


std::vector<AirtimeQueueLimit> WifiAirtime::tightened_limits() {
    return {
        {0, 2000, 5000},
        {1, 2000, 5000},
        {2, 3000, 8000},
        {3, 3000, 8000},
    };
}


std::string WifiAirtime::read_first_line(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";

    std::string line;
    std::getline(f, line);

    return line;
}


bool WifiAirtime::write_line(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) return false;

    f << value;

    return true;
}


std::string WifiAirtime::phy_name_for(const std::string& iface) {
    if (iface.empty()) return "";

    std::string name = read_first_line("/sys/class/net/" + iface + "/phy80211/name");

    while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) {
        name.pop_back();
    }

    return name;
}


std::string WifiAirtime::phy_debugfs_path(const std::string& phy) {
    return "/sys/kernel/debug/ieee80211/" + phy;
}


bool WifiAirtime::debugfs_available() {
    return std::filesystem::exists("/sys/kernel/debug/ieee80211");
}


std::vector<AirtimeQueueLimit> WifiAirtime::read_queue_limits(const std::string& phy_path) {
    std::vector<AirtimeQueueLimit> limits;
    std::ifstream f(phy_path + "/aql_txq_limit");

    if (!f) return limits;

    std::string line;
    std::getline(f, line);

    int access_category = 0;

    while (std::getline(f, line) && access_category < 4) {
        std::istringstream ss(line);
        std::string label;
        int low = 0;
        int high = 0;

        ss >> label >> low >> high;

        if (ss.fail() || low <= 0 || high <= 0) return {};

        limits.push_back({access_category, low, high});
        
        ++access_category;
    }

    return limits;
}


void WifiAirtime::write_queue_limit(const std::string& phy_path, const AirtimeQueueLimit& limit) {
    std::string value = std::to_string(limit.access_category) + " " + std::to_string(limit.low_us) + " " + std::to_string(limit.high_us);

    write_line(phy_path + "/aql_txq_limit", value);
}


std::string WifiAirtime::serialize_limits(const std::vector<AirtimeQueueLimit>& limits) {
    std::ostringstream ss;

    for (const auto& limit : limits) {
        ss << limit.access_category << " " << limit.low_us << " " << limit.high_us << "\n";
    }

    return ss.str();
}


std::vector<AirtimeQueueLimit> WifiAirtime::deserialize_limits(const std::string& raw) {
    std::vector<AirtimeQueueLimit> limits;
    std::istringstream ss(raw);
    std::string line;

    while (std::getline(ss, line)) {
        std::istringstream line_stream(line);
        int access_category = 0;
        int low = 0;
        int high = 0;

        line_stream >> access_category >> low >> high;

        if (line_stream.fail()) continue;

        limits.push_back({access_category, low, high});
    }

    return limits;
}


void WifiAirtime::apply(const std::string& iface) {
    std::string phy = phy_name_for(iface);
    if (phy.empty()) return;

    if (!debugfs_available()) {
        system("mount -t debugfs none /sys/kernel/debug 2>/dev/null");

        if (!debugfs_available()) return;
    }

    std::string phy_path = phy_debugfs_path(phy);
    if (!std::filesystem::exists(phy_path + "/aql_txq_limit")) return;

    // The daemon reapplies this on every boot, so the saved baseline must never be overwritten with our own values
    if (!Storage::exists("aql_saved_limits")) {
        auto current = read_queue_limits(phy_path);

        if (current.empty()) return;

        Storage::save("aql_saved_limits", serialize_limits(current));
        Storage::save("aql_phy", phy);

        std::string threshold = read_first_line(phy_path + "/aql_threshold");

        if (!threshold.empty()) Storage::save("aql_saved_threshold", threshold);
    }

    for (const auto& limit : tightened_limits()) {
        write_queue_limit(phy_path, limit);
    }

    write_line(phy_path + "/aql_threshold", std::to_string(kTightenedThresholdUs));
}


void WifiAirtime::revert() {
    if (!Storage::exists("aql_phy")) return;

    std::string phy_path = phy_debugfs_path(Storage::load("aql_phy"));

    if (Storage::exists("aql_saved_limits")) {
        for (const auto& limit : deserialize_limits(Storage::load("aql_saved_limits"))) {
            write_queue_limit(phy_path, limit);
        }

        Storage::remove_file("aql_saved_limits");
    }

    if (Storage::exists("aql_saved_threshold")) {
        write_line(phy_path + "/aql_threshold", Storage::load("aql_saved_threshold"));
        Storage::remove_file("aql_saved_threshold");
    }

    Storage::remove_file("aql_phy");
}
}