// core/src/tuner.cpp
#include "betterconn/tuner.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>





namespace betterconn {
static constexpr int kIntervalMs = 3000;
static constexpr double kRttHighMs = 60.0;
static constexpr double kRttLowMs = 25.0;
static constexpr double kRetryHighPct = 5.0;
static constexpr double kRetryLowPct = 1.0;
static constexpr double kRssiPoorDbm = -75.0;
static constexpr double kRssiFairDbm = -65.0;

void Tuner::write_sysctl(const std::string& key, const std::string& value) {
    std::string path = "/proc/sys/";
    for (char c : key) path += (c == '.') ? '/' : c;
    std::ofstream f(path);
    if (f) f << value << "\n";
}

void Tuner::set_qdisc_target(const std::string& iface, int target_ms) {
    std::string cmd = "tc qdisc replace dev " + iface +
        " root fq_codel target " + std::to_string(target_ms) + "ms"
        " interval " + std::to_string(target_ms * 20) + "ms 2>/dev/null";
    system(cmd.c_str());
}

double Tuner::read_rtt_ms() {
    FILE* p = popen("ping -c 2 -i 0.1 -W 1 8.8.8.8 2>/dev/null", "r");
    if (!p) return -1.0;
    
    std::string out;
    char buf[256];

    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    
    auto pos = out.find("rtt min/avg/max");
    if (pos == std::string::npos) return -1.0;
    
    auto eq = out.find("= ", pos);
    if (eq == std::string::npos) return -1.0;
    
    auto s = eq + 2;
    auto sl1 = out.find('/', s);
    auto sl2 = (sl1 != std::string::npos) ? out.find('/', sl1 + 1) : std::string::npos;
    if (sl2 == std::string::npos) return -1.0;

    try { return std::stod(out.substr(sl1 + 1, sl2 - sl1 - 1)); } catch (...) { return -1.0; }
}

double Tuner::read_tx_retries(const std::string& iface) {
    std::ifstream f("/proc/net/wireless");
    if (!f) return 0.0;
    
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string name = line.substr(0, colon);
        
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);
        
        if (name != iface) continue;
        
        std::istringstream ss(line.substr(colon + 1));
        double status, link, level, noise, nwid, crypt, frag, retry;
        ss >> status >> link >> level >> noise >> nwid >> crypt >> frag >> retry;
        return retry;
    }
    return 0.0;
}

double Tuner::read_rx_bps(const std::string& iface) {
    auto read_rx = [&]() -> uint64_t {
        std::ifstream f("/proc/net/dev");
        std::string line;
        
        while (std::getline(f, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            
            std::string name = line.substr(0, colon);
            
            auto trim = name.find_first_not_of(' ');
            if (trim != std::string::npos) name = name.substr(trim);
            
            if (name != iface) continue;
            std::istringstream ss(line.substr(colon + 1));
            uint64_t rx;
            ss >> rx;
            return rx;
        }
        return 0ULL;
    };
    uint64_t before = read_rx();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    uint64_t after = read_rx();
    return static_cast<double>(after - before) * 8.0 / 0.5;
}

double Tuner::read_rssi_dbm(const std::string& iface) {
    std::ifstream f("/proc/net/wireless");
    if (!f) return 0.0;
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string name = line.substr(0, colon);
        
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);
        
        if (name != iface) continue;
        std::istringstream ss(line.substr(colon + 1));
        double status, link, level;
        ss >> status >> link >> level;
        return level;
    }
    return 0.0;
}

bool Tuner::is_wifi(const std::string& iface) {
    std::ifstream f("/sys/class/net/" + iface + "/phy80211");
    return f.good() || (system(("test -d /sys/class/net/" + iface + "/phy80211 2>/dev/null").c_str()) == 0);
}

void Tuner::loop(const std::string& iface, std::atomic<bool>& running) {
    const bool wifi = is_wifi(iface);

    double prev_retries = 0.0;

    while (running.load(std::memory_order_relaxed)) {
        double rtt = read_rtt_ms();
        double retries = wifi ? read_tx_retries(iface) : 0.0;
        double rssi = wifi ? read_rssi_dbm(iface) : 0.0;
        double retry_delta = retries - prev_retries;
        prev_retries = retries;

        double rx_bps = read_rx_bps(iface);
        uint64_t buf_max;
        
        if (rx_bps > 100.0 * 1024.0 * 1024.0) {
            buf_max = 33554432;
        } else if (rx_bps > 20.0 * 1024.0 * 1024.0) {
            buf_max = 16777216;
        } else if (rx_bps > 5.0 * 1024.0 * 1024.0) {
            buf_max = 8388608;
        } else {
            buf_max = 4194304;
        }
        
        write_sysctl("net.core.rmem_max", std::to_string(buf_max));
        write_sysctl("net.core.wmem_max", std::to_string(buf_max));
        uint64_t buf_default = buf_max / 8;
        write_sysctl("net.core.rmem_default", std::to_string(buf_default));
        write_sysctl("net.core.wmem_default", std::to_string(buf_default));

        std::string tcp_rmem = "4096 " + std::to_string(buf_default) + " " + std::to_string(buf_max);
        write_sysctl("net.ipv4.tcp_rmem", tcp_rmem);
        write_sysctl("net.ipv4.tcp_wmem", tcp_rmem);

        int qdisc_target_ms = 5;
        std::string notsent_lowat = "131072";

        if (wifi) {
            bool poor_signal = (rssi != 0.0 && rssi < kRssiPoorDbm);
            bool fair_signal = (rssi != 0.0 && rssi < kRssiFairDbm);
            bool high_retry = (retry_delta > kRetryHighPct);
            bool low_retry = (retry_delta < kRetryLowPct);

            if (poor_signal || high_retry) {
                qdisc_target_ms = 10;
                notsent_lowat = "32768";
                write_sysctl("net.ipv4.tcp_autocorking", "1");
                write_sysctl("net.core.netdev_budget", "300");
            } else if (fair_signal || (rtt > kRttHighMs && rtt > 0)) {
                qdisc_target_ms = 7;
                notsent_lowat = "65536";
                write_sysctl("net.ipv4.tcp_autocorking", "0");
                write_sysctl("net.core.netdev_budget", "400");
            } else if (low_retry && rtt > 0 && rtt < kRttLowMs) {
                qdisc_target_ms = 4;
                notsent_lowat = "131072";
                write_sysctl("net.ipv4.tcp_autocorking", "0");
                write_sysctl("net.core.netdev_budget", "600");
            } else {
                qdisc_target_ms = 5;
                notsent_lowat = "131072";
                write_sysctl("net.ipv4.tcp_autocorking", "0");
                write_sysctl("net.core.netdev_budget", "500");
            }
        } else {
            if (rtt > 0 && rtt > kRttHighMs) {
                qdisc_target_ms = 8;
                notsent_lowat = "65536";
                write_sysctl("net.core.netdev_budget", "400");
            } else if (rtt > 0 && rtt < kRttLowMs) {
                qdisc_target_ms = 4;
                notsent_lowat = "131072";
                write_sysctl("net.core.netdev_budget", "600");
            } else {
                qdisc_target_ms = 5;
                notsent_lowat = "131072";
                write_sysctl("net.core.netdev_budget", "500");
            }
        }

        write_sysctl("net.ipv4.tcp_notsent_lowat", notsent_lowat);
        set_qdisc_target(iface, qdisc_target_ms);

        std::this_thread::sleep_for(std::chrono::milliseconds(kIntervalMs));
    }
}

void Tuner::start(const std::string& iface) {
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread([iface, this]() {
        loop(iface, running_);
    });
}

void Tuner::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
}
}