// core/src/optimizer.cpp
#include "betterconn/optimizer.hpp"
#include "betterconn/storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>





namespace betterconn {
static std::vector<SysctlParam> base_params() {
    return {
        {"net.core.default_qdisc", "fq_codel"},
        {"net.ipv4.tcp_congestion_control", "bbr"},
        {"net.core.rmem_max", "16777216"},
        {"net.core.wmem_max", "16777216"},
        {"net.core.rmem_default", "1048576"},
        {"net.core.wmem_default", "1048576"},
        {"net.ipv4.tcp_rmem", "4096 1048576 16777216"},
        {"net.ipv4.tcp_wmem", "4096 1048576 16777216"},
        {"net.core.netdev_max_backlog", "5000"},
        {"net.core.netdev_budget", "500"},
        {"net.core.netdev_budget_usecs", "8000"},
        {"net.ipv4.tcp_fastopen", "3"},
        {"net.ipv4.tcp_window_scaling", "1"},
        {"net.ipv4.tcp_timestamps", "1"},
        {"net.ipv4.tcp_sack", "1"},
        {"net.ipv4.tcp_ecn", "1"},
        {"net.ipv4.tcp_fin_timeout", "15"},
        {"net.ipv4.tcp_tw_reuse", "1"},
        {"net.ipv4.tcp_autocorking", "0"},
        {"net.ipv4.tcp_notsent_lowat", "131072"},
        {"net.ipv4.tcp_slow_start_after_idle", "0"},
        {"net.ipv4.tcp_mtu_probing", "1"},
        {"net.ipv4.ip_local_port_range", "1024 65535"},
        {"net.ipv4.tcp_rto_min_us", "5000"},
        {"net.ipv4.tcp_keepalive_time", "60"},
        {"net.ipv4.tcp_keepalive_intvl", "10"},
        {"net.ipv4.tcp_keepalive_probes", "6"},
    };
}

std::string Optimizer::detect_interface() {
    std::ifstream f("/proc/net/route");
    
    if (!f) return "";
    
    std::string line;
    std::getline(f, line);
    
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface, dest;
        ss >> iface >> dest;
        
        if (dest == "00000000") return iface;
    }
    return "";
}

bool Optimizer::is_wifi(const std::string& iface) {
    return std::filesystem::exists("/sys/class/net/" + iface + "/phy80211");
}

std::string Optimizer::sysctl_path(const std::string& key) {
    std::string path = "/proc/sys/";
    
    for (char c : key) path += (c == '.') ? '/' : c;
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
        std::string check = rule;
        
        auto pos = check.find(" -A ");
        if (pos != std::string::npos) check.replace(pos, 4, " -C ");
        
        std::string cmd = check + " 2>/dev/null || " + rule + " 2>/dev/null";
        if (system(cmd.c_str()) != 0) ++failed;
    }
    if (failed > 0) {
        std::cerr << "[!!] " << failed << " iptables QoS rule(s) could not be applied (non-critical)\n";
    }
}

void Optimizer::revert_iptables() {
    for (auto rule : iptables_add_rules()) {
        auto pos = rule.find(" -A ");
        
        if (pos != std::string::npos) rule.replace(pos, 4, " -D ");
        
        system((rule + " 2>/dev/null").c_str());
    }
}

void Optimizer::apply_nic_tuning(const std::string& iface) {
    if (iface.empty()) return;
    
    if (system("command -v ethtool >/dev/null 2>&1") != 0) {
        std::cerr << "[!!] ethtool not found, skipping NIC tuning\n";
        return;
    }
    
    std::string cmd = "ethtool -c " + iface + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return;
    
    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    pclose(p);

    bool arx_on = output.find("Adaptive RX: on") != std::string::npos;
    bool atx_on = output.find("Adaptive TX: on") != std::string::npos;

    Storage::save("nic_iface", iface);
    Storage::save("nic_adaptive_rx", arx_on ? "on" : "off");
    Storage::save("nic_adaptive_tx", atx_on ? "on" : "off");

    if (!arx_on || !atx_on) {
        std::string set_cmd = "ethtool -C " + iface + " adaptive-rx on adaptive-tx on 2>/dev/null";
        
        if (system(set_cmd.c_str()) != 0) {
            std::cerr << "[!!] could not set adaptive interrupt coalescing on " << iface << " (non-critical)\n";
        }
    }
}

void Optimizer::revert_nic_tuning() {
    if (!Storage::exists("nic_iface")) return;

    if (system("command -v ethtool >/dev/null 2>&1") != 0) return;
    
    std::string iface = Storage::load("nic_iface");
    std::string arx = Storage::exists("nic_adaptive_rx") ? Storage::load("nic_adaptive_rx") : "off";
    std::string atx = Storage::exists("nic_adaptive_tx") ? Storage::load("nic_adaptive_tx") : "off";
    std::string cmd = "ethtool -C " + iface + " adaptive-rx " + arx + " adaptive-tx " + atx + " 2>/dev/null";
    
    system(cmd.c_str());
    
    Storage::remove_file("nic_iface");
    Storage::remove_file("nic_adaptive_rx");
    Storage::remove_file("nic_adaptive_tx");
}

void Optimizer::apply_interface_qdisc(const std::string& iface) {
    if (iface.empty()) return;
    
    bool ok = system(("tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;
    if (!ok) {
        std::cerr << "[!!] could not set fq_codel on " << iface << " (non-critical)\n";
        return;
    }
    
    Storage::save("tc_iface", iface);
}

void Optimizer::revert_interface_qdisc(const std::string& iface) {
    if (iface.empty()) return;
    
    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    
    Storage::remove_file("tc_iface");
}

void Optimizer::apply_wifi_latency(const std::string& iface) {
    if (iface.empty() || !is_wifi(iface)) return;

    FILE* p = popen(("iw dev " + iface + " get power_save 2>/dev/null").c_str(), "r");
    
    std::string original_pm = "on";
    
    if (p) {
        char buf[64];
        
        if (fgets(buf, sizeof(buf), p)) {
            if (std::string(buf).find("off") != std::string::npos) original_pm = "off";
        }
        pclose(p);
    }

    Storage::save("wifi_pm_original", original_pm);
    Storage::save("wifi_pm_iface", iface);

    system(("iw dev " + iface + " set power_save off 2>/dev/null").c_str());

    std::error_code ec;
    std::filesystem::create_directories("/etc/NetworkManager/conf.d", ec);
    std::ofstream f("/etc/NetworkManager/conf.d/betterconn.conf");
    
    if (f) {
        f << "[connection]\n";
        f << "wifi.powersave = 2\n";
    }

    system("nmcli general reload 2>/dev/null");
}

void Optimizer::revert_wifi_latency() {
    if (!Storage::exists("wifi_pm_iface")) return;
    
    std::string iface = Storage::load("wifi_pm_iface");
    std::string pm = Storage::exists("wifi_pm_original") ? Storage::load("wifi_pm_original") : "on";
    
    system(("iw dev " + iface + " set power_save " + pm + " 2>/dev/null").c_str());
    
    Storage::remove_file("wifi_pm_original");
    Storage::remove_file("wifi_pm_iface");
    
    std::error_code ec;
    std::filesystem::remove("/etc/NetworkManager/conf.d/betterconn.conf", ec);
    system("nmcli general reload 2>/dev/null");
}

void Optimizer::apply_dns() {
    if (system("systemctl is-active --quiet systemd-resolved 2>/dev/null") != 0) return;
    
    std::error_code ec;
    std::filesystem::create_directories("/etc/systemd/resolved.conf.d", ec);
    std::ofstream f("/etc/systemd/resolved.conf.d/betterconn.conf");
    
    if (!f) {
        std::cerr << "[!!] could not configure DNS resolver (non-critical)\n";
        return;
    }
    
    f << "[Resolve]\n";
    f << "DNS=1.1.1.1 1.0.0.1 8.8.8.8 8.8.4.4\n";
    f << "Cache=yes\n";
    f << "DNSStubListener=yes\n";
    system("systemctl restart systemd-resolved 2>/dev/null");
}

void Optimizer::revert_dns() {
    std::error_code ec;
    
    if (std::filesystem::remove("/etc/systemd/resolved.conf.d/betterconn.conf", ec)) {
        system("systemctl restart systemd-resolved 2>/dev/null");
    }
}

void Optimizer::write_persistence(const std::string& iface) {
    std::error_code ec;
    std::filesystem::create_directories("/etc/betterconn", ec);

    {
        std::ofstream f("/etc/modules-load.d/betterconn.conf");
        
        if (f) {
            f << "tcp_bbr\n";
            f << "xt_TOS\n";
        } else {
            std::cerr << "[!!] could not write /etc/modules-load.d/betterconn.conf (non-critical)\n";
        }
    }

    {
        std::ofstream f("/etc/sysctl.d/99-betterconn.conf");
        
        if (f) {
            for (const auto& p : base_params()) {
                f << p.key << " = " << p.value << "\n";
            }
        } else {
            std::cerr << "[!!] could not write /etc/sysctl.d/99-betterconn.conf (non-critical)\n";
        }
    }

    {
        std::ofstream f("/etc/betterconn/iptables-apply.sh");
        
        if (f) {
            f << "#!/bin/sh\n";
            f << "modprobe xt_TOS 2>/dev/null\n";
            
            for (const auto& rule : iptables_add_rules()) {
                std::string check = rule;
                
                auto pos = check.find(" -A ");
                if (pos != std::string::npos) check.replace(pos, 4, " -C ");
                
                f << check << " 2>/dev/null || " << rule << " 2>/dev/null\n";
            }
            
            if (!iface.empty()) {
                f << "ethtool -C " << iface << " adaptive-rx on adaptive-tx on 2>/dev/null || true\n";
                f << "tc qdisc replace dev " << iface << " root fq_codel target 5ms interval 100ms 2>/dev/null || true\n";
                
                if (is_wifi(iface)) {
                    f << "iw dev " << iface << " set power_save off 2>/dev/null || true\n";
                }
            }
        }
    }
    
    system("chmod 755 /etc/betterconn/iptables-apply.sh 2>/dev/null");

    {
        std::ofstream f("/etc/systemd/system/betterconn.service");
        
        if (f) {
            f << "[Unit]\n"
              << "Description=betterconn network optimizer\n"
              << "After=network.target\n"
              << "\n"
              << "[Service]\n"
              << "Type=simple\n"
              << "ExecStartPre=/bin/sh /etc/betterconn/iptables-apply.sh\n"
              << "ExecStart=/usr/bin/betterconn daemon\n"
              << "Restart=on-failure\n"
              << "RestartSec=5\n"
              << "\n"
              << "[Install]\n"
              << "WantedBy=multi-user.target\n";
        } else {
            std::cerr << "[!!] could not write betterconn.service (non-critical)\n";
        }
    }

    system("systemctl daemon-reload 2>/dev/null");
    
    if (system("systemctl enable betterconn.service 2>/dev/null") != 0) {
        std::cerr << "[!!] could not enable betterconn.service (non-critical)\n";
    }
}

void Optimizer::remove_persistence() {
    system("systemctl stop betterconn.service 2>/dev/null");
    system("systemctl disable betterconn.service 2>/dev/null");
    system("systemctl reset-failed betterconn.service 2>/dev/null");
    std::error_code ec;
    std::filesystem::remove("/etc/systemd/system/betterconn.service", ec);
    std::filesystem::remove_all("/etc/betterconn", ec);
    std::filesystem::remove("/etc/sysctl.d/99-betterconn.conf", ec);
    std::filesystem::remove("/etc/modules-load.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/NetworkManager/conf.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/systemd/resolved.conf.d/betterconn.conf", ec);
    system("systemctl daemon-reload 2>/dev/null");
}

void Optimizer::apply(const std::string& forced_iface) {
    if (is_active()) {
        throw std::runtime_error("betterconn is already active, run stop first");
    }

    std::string iface = forced_iface.empty() ? detect_interface() : forced_iface;

    load_bbr_module();

    std::string avail;
    {
        std::ifstream f("/proc/sys/net/ipv4/tcp_available_congestion_control");
        
        if (f) std::getline(f, avail);
    }
    
    bool bbr_available = avail.find("bbr") != std::string::npos;
    if (!bbr_available) {
        std::cerr << "[!!] tcp_bbr module not available, congestion control will remain at default\n";
    }

    auto params = base_params();

    std::ostringstream backup;
    
    for (const auto& p : params) {
        backup << p.key << "=" << read_sysctl(p.key) << "\n";
    }
    
    Storage::save("sysctl_backup", backup.str());

    for (const auto& p : params) {
        if (!bbr_available && p.key == "net.ipv4.tcp_congestion_control") continue;
        
        if (!write_sysctl(p.key, p.value)) {
            std::cerr << "[!!] could not set " << p.key << " (not supported by this kernel)\n";
        }
    }

    apply_interface_qdisc(iface);
    apply_wifi_latency(iface);
    apply_nic_tuning(iface);
    write_persistence(iface);
    apply_iptables();
    apply_dns();
    Storage::save("iface", iface);
    Storage::save("state", "active");
}

void Optimizer::revert() {
    if (!is_active()) {
        throw std::runtime_error("betterconn is not active");
    }
    
    if (!Storage::exists("sysctl_backup")) {
        throw std::runtime_error("sysctl backup not found, cannot revert safely");
    }

    system("systemctl stop betterconn.service 2>/dev/null");

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

    if (Storage::exists("tc_iface")) {
        revert_interface_qdisc(Storage::load("tc_iface"));
    }

    revert_wifi_latency();
    revert_nic_tuning();
    revert_dns();
    remove_persistence();
    Storage::save("state", "inactive");
    Storage::remove_file("sysctl_backup");
    Storage::remove_file("iface");
}

bool Optimizer::is_active() const {
    if (!Storage::exists("state")) return false;
    
    return Storage::load("state") == "active";
}
}