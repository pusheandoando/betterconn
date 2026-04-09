// core/src/status.cpp
#include "betterconn/status.hpp"
#include "betterconn/storage.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>





namespace betterconn {
std::string Status::detect_interface() {
    std::ifstream f("/proc/net/route");
    
    if (!f) return "unknown";
    
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface, dest;
        ss >> iface >> dest;
        if (dest == "00000000") return iface;
    }
    return "unknown";
}

std::pair<double, double> Status::measure_speed(const std::string& iface) {
    auto read_bytes = [&]() -> std::pair<uint64_t, uint64_t> {
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
            
            uint64_t rx, tx, dummy;
            ss >> rx;
            for (int i = 0; i < 7; ++i) ss >> dummy;
            ss >> tx;
            return {rx, tx};
        }
        return {0, 0};
    };

    auto [rx0, tx0] = read_bytes();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto [rx1, tx1] = read_bytes();

    return {static_cast<double>(rx1 - rx0), static_cast<double>(tx1 - tx0)};
}

double Status::measure_ping(const std::string& host) {
    std::string cmd = "ping -c 3 -i 0.2 -W 2 " + host + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    
    if (!p) return -1.0;
    
    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    pclose(p);

    auto pos = output.find("rtt min/avg/max");
    if (pos == std::string::npos) return -1.0;
    
    auto eq_pos = output.find("= ", pos);
    if (eq_pos == std::string::npos) return -1.0;
    
    auto start = eq_pos + 2;
    auto slash1 = output.find('/', start);
    if (slash1 == std::string::npos) return -1.0;
    auto slash2 = output.find('/', slash1 + 1);
    if (slash2 == std::string::npos) return -1.0;
    
    try {
        return std::stod(output.substr(slash1 + 1, slash2 - slash1 - 1));
    } catch (...) {
        return -1.0;
    }
}

std::string Status::read_sysctl(const std::string& key) {
    std::string path = "/proc/sys/";
    
    for (char c : key) path += (c == '.') ? '/' : c;
    
    std::ifstream f(path);
    if (!f) return "n/a";
    
    std::string val;
    std::getline(f, val);
    return val;
}

void Status::print() const {
    bool active = Storage::exists("state") && Storage::load("state") == "active";
    std::string iface = detect_interface();

    auto fmt_speed = [](double bytes) -> std::string {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        
        if (bytes >= 1024.0 * 1024.0) {
            ss << bytes / (1024.0 * 1024.0) << " MB/s";
        } else if (bytes >= 1024.0) {
            ss << bytes / 1024.0 << " KB/s";
        } else {
            ss << bytes << " B/s";
        }
        return ss.str();
    };

    std::cout << "\nbetterconn status\n";
    std::cout << "-----------------\n";
    std::cout << "State:            " << (active ? "ACTIVE" : "INACTIVE") << "\n";
    std::cout << "Interface:        " << iface << "\n";

    if (iface != "unknown") {
        std::cout << "Sampling speeds (1s)..." << std::flush;
        auto [down, up] = measure_speed(iface);
        std::cout << "\r                       \r";
        std::cout << "Download:         " << fmt_speed(down) << "\n";
        std::cout << "Upload:           " << fmt_speed(up) << "\n";
    }

    double ping = measure_ping("8.8.8.8");
    std::cout << std::fixed << std::setprecision(2);
    if (ping >= 0.0) {
        std::cout << "Ping (8.8.8.8):   " << ping << " ms avg\n";
    } else {
        std::cout << "Ping (8.8.8.8):   unreachable\n";
    }

    std::cout << "Congestion ctrl:  " << read_sysctl("net.ipv4.tcp_congestion_control") << "\n";
    std::cout << "Queue discipline: " << read_sysctl("net.core.default_qdisc") << "\n";
    std::cout << "TCP Fast Open:    " << read_sysctl("net.ipv4.tcp_fastopen") << "\n";
    std::cout << "RX buffer max:    " << read_sysctl("net.core.rmem_max") << " bytes\n";
    std::cout << "TX buffer max:    " << read_sysctl("net.core.wmem_max") << " bytes\n";
    std::cout << "Fin timeout:      " << read_sysctl("net.ipv4.tcp_fin_timeout") << " s\n";
    std::cout << "Slow start idle:  " << read_sysctl("net.ipv4.tcp_slow_start_after_idle") << "\n";
    std::cout << "\n";
}
}