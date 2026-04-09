// core/src/optimizer.cpp
#include "betterconn/optimizer.hpp"
#include "betterconn/storage.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>





namespace betterconn {
std::vector<SysctlParam> Optimizer::target_params() {
    return {
        {"net.core.default_qdisc", "fq"},
        {"net.ipv4.tcp_congestion_control", "bbr"},
        {"net.core.rmem_max", "16777216"},
        {"net.core.wmem_max", "16777216"},
        {"net.ipv4.tcp_rmem", "4096 87380 16777216"},
        {"net.ipv4.tcp_wmem", "4096 65536 16777216"},
        {"net.core.netdev_max_backlog", "5000"},
        {"net.ipv4.tcp_fastopen", "3"},
        {"net.ipv4.tcp_window_scaling", "1"},
        {"net.ipv4.tcp_timestamps", "1"},
        {"net.ipv4.tcp_sack", "1"},
        {"net.ipv4.tcp_fin_timeout", "15"},
        {"net.ipv4.tcp_tw_reuse", "1"},
        {"net.ipv4.tcp_notsent_lowat", "131072"},
        {"net.ipv4.tcp_slow_start_after_idle", "0"},
        {"net.ipv4.tcp_mtu_probing", "1"},
        {"net.ipv4.ip_local_port_range", "1024 65535"},
    };
}

std::string Optimizer::sysctl_path(const std::string& key) {
    std::string path = "/proc/sys/";
    
    for (char c : key) {
        path += (c == '.') ? '/' : c;
    }
    return path;
}

std::string Optimizer::read_sysctl(const std::string& key) {
    std::ifstream f(sysctl_path(key));
    
    if (!f) return "";
    
    std::string val;
    std::getline(f, val);
    return val;
}

bool Optimizer::write_sysctl(const std::string& key, const std::string& value) {
    std::ofstream f(sysctl_path(key));
    
    if (!f) return false;
    
    f << value << "\n";
    return true;
}

void Optimizer::load_bbr_module() {
    system("modprobe tcp_bbr 2>/dev/null");
}

std::vector<std::string> Optimizer::iptables_add_rules() {
    return {
        "iptables -t mangle -A OUTPUT -p udp --dport 53 -j TOS --set-tos 0x10",
        "iptables -t mangle -A OUTPUT -p tcp --dport 80 -j TOS --set-tos 0x10",
        "iptables -t mangle -A OUTPUT -p tcp --dport 443 -j TOS --set-tos 0x10",
        "iptables -t mangle -A OUTPUT -p udp --dport 443 -j TOS --set-tos 0x10",
        "iptables -t mangle -A OUTPUT -p udp --dport 27000:27030 -j TOS --set-tos 0x10",
        "iptables -t mangle -A OUTPUT -p udp --dport 3478:3480 -j TOS --set-tos 0x10",
    };
}

void Optimizer::apply_iptables() {
    system("modprobe xt_TOS 2>/dev/null");
    int failed = 0;
    
    for (const auto& rule : iptables_add_rules()) {
        if (system((rule + " 2>/dev/null").c_str()) != 0) {
            ++failed;
        }
    }
    
    if (failed > 0) {
        std::cerr << "[!!] " << failed << " iptables QoS rule(s) could not be applied (non-critical)\n";
    }
}

void Optimizer::revert_iptables() {
    for (auto rule : iptables_add_rules()) {
        auto pos = rule.find(" -A ");
        
        if (pos != std::string::npos) {
            rule.replace(pos, 4, " -D ");
        }
        
        system((rule + " 2>/dev/null").c_str());
    }
}

void Optimizer::apply() {
    if (is_active()) {
        throw std::runtime_error("[!!] betterconn is already active, run --stop first");
    }

    load_bbr_module();

    std::string avail;
    {
        std::ifstream f("/proc/sys/net/ipv4/tcp_available_congestion_control");
        if (f) std::getline(f, avail);
    }
    
    bool bbr_available = avail.find("bbr") != std::string::npos;
    if (!bbr_available) {
        std::cerr << "[!!] tcp_bbr module not available on this kernel, skipping congestion control change\n";
    }

    std::ostringstream backup;
    
    for (const auto& p : target_params()) {
        std::string current = read_sysctl(p.key);
        backup << p.key << "=" << current << "\n";
    }
    Storage::save("sysctl_backup", backup.str());

    for (const auto& p : target_params()) {
        if (!bbr_available && (p.key == "net.ipv4.tcp_congestion_control" || p.key == "net.core.default_qdisc")) {
            continue;
        }

        if (!write_sysctl(p.key, p.value)) {
            std::cerr << "[!!] could not set " << p.key << " (not supported by this kernel)\n";
        }
    }

    apply_iptables();
    Storage::save("state", "active");
}

void Optimizer::revert() {
    if (!is_active()) {
        throw std::runtime_error("[!!] betterconn is not active");
    }
    
    if (!Storage::exists("sysctl_backup")) {
        throw std::runtime_error("[!!] sysctl backup not found, cannot revert safely");
    }

    std::string backup = Storage::load("sysctl_backup");
    std::istringstream ss(backup);
    std::string line;
    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (!write_sysctl(key, value)) {
            std::cerr << "[!!] could not restore " << key << "\n";
        }
    }

    revert_iptables();

    Storage::save("state", "inactive");
    Storage::remove_file("sysctl_backup");
}

bool Optimizer::is_active() const {
    if (!Storage::exists("state")) return false;
    
    return Storage::load("state") == "active";
}
}