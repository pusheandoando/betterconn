// core/src/optimizer.cpp
#include "betterconn/storage.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/wifi_airtime.hpp"
#include "betterconn/irq_affinity.hpp"
#include "betterconn/packet_marking.hpp"
#include "betterconn/system_defaults.hpp"
#include "betterconn/priority_scheduler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>





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

        {"net.ipv4.udp_rmem_min", "65536"},
        {"net.ipv4.udp_wmem_min", "65536"},

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

        {"net.ipv4.tcp_moderate_rcvbuf", "1"},
        {"net.ipv4.tcp_recovery", "1"},

        // WiFi delay spikes routinely fire spurious retransmit timeouts, and F-RTO unwinds them instead of letting the sender collapse its window on a loss that never happened
        {"net.ipv4.tcp_frto", "2"},

        // Game and voice streams never keep enough packets in flight to trigger fast retransmit, so linear timeouts are what actually bounds their recovery time
        {"net.ipv4.tcp_thin_linear_timeouts", "1"},

        // Cached metrics from a previously congested path throttle the initial window of every new connection to the same host, which is exactly the part that decides how fast a page starts
        {"net.ipv4.tcp_no_metrics_save", "1"},
    };
}


std::string Optimizer::ethtool_coalesce_args(LatencyProfile profile) {
    switch (profile) {
        case LatencyProfile::Latency:
            // Asking for one interrupt per frame is rejected by most drivers and becomes an interrupt storm on the rest
            return "adaptive-rx off adaptive-tx off rx-usecs 8 rx-frames 8 tx-usecs 16 tx-frames 16";
        case LatencyProfile::Throughput:
            return "adaptive-rx off adaptive-tx off rx-usecs 250 rx-frames 64 tx-usecs 250 tx-frames 64";
        case LatencyProfile::Balanced:
        default:
            return "adaptive-rx on adaptive-tx on";
    }
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


void Optimizer::load_kernel_modules() {
    system("modprobe tcp_bbr 2>/dev/null");
    system("modprobe sch_cake 2>/dev/null");

    PacketMarking::load_required_modules();
}


std::string Optimizer::coalesce_field(const std::string& output, const std::string& key) {
    auto pos = output.find(key);
    if (pos == std::string::npos) return "";

    auto value_start = output.find_first_not_of(" \t", pos + key.size());
    if (value_start == std::string::npos) return "";

    auto value_end = output.find_first_of(" \t\r\n", value_start);

    return output.substr(value_start, value_end - value_start);
}


void Optimizer::save_nic_coalesce_state(const std::string& iface) {
    std::string cmd = "ethtool -c " + iface + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return;

    std::string output;
    char buf[256];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    // ethtool prints both flags on one line as "Adaptive RX: off  TX: off", so the TX flag only resolves relative to that line
    std::string adaptive_rx = coalesce_field(output, "Adaptive RX:");
    std::string adaptive_tx;

    auto adaptive_pos = output.find("Adaptive RX:");

    if (adaptive_pos != std::string::npos) {
        adaptive_tx = coalesce_field(output.substr(adaptive_pos), "TX:");
    }

    Storage::save("nic_iface", iface);
    Storage::save("nic_adaptive_rx", adaptive_rx.empty() ? "off" : adaptive_rx);
    Storage::save("nic_adaptive_tx", adaptive_tx.empty() ? "off" : adaptive_tx);
    Storage::save("nic_rx_usecs", coalesce_field(output, "rx-usecs:"));
    Storage::save("nic_rx_frames", coalesce_field(output, "rx-frames:"));
    Storage::save("nic_tx_usecs", coalesce_field(output, "tx-usecs:"));
    Storage::save("nic_tx_frames", coalesce_field(output, "tx-frames:"));
}


void Optimizer::apply_nic_tuning(const std::string& iface, LatencyProfile profile) {
    if (iface.empty()) return;
    
    if (system("command -v ethtool >/dev/null 2>&1") != 0) {
        std::cerr << "[!!] ethtool not found, skipping NIC tuning\n";
        return;
    }

    save_nic_coalesce_state(iface);

    std::string set_cmd = "ethtool -C " + iface + " " + ethtool_coalesce_args(profile) + " 2>/dev/null";

    if (system(set_cmd.c_str()) == 0) return;

    // ethtool rejects the whole request when a single parameter is unsupported, so the driver managed mode is the fallback
    std::string fallback_cmd = "ethtool -C " + iface + " adaptive-rx on adaptive-tx on 2>/dev/null";

    if (system(fallback_cmd.c_str()) != 0) {
        std::cerr << "[!!] could not set interrupt coalescing on " << iface << " (non-critical)\n";
    }
}


void Optimizer::revert_nic_tuning() {
    if (!Storage::exists("nic_iface")) return;

    if (system("command -v ethtool >/dev/null 2>&1") != 0) return;

    struct CoalesceCounter {
        const char* storage_key;
        const char* ethtool_name;
    };

    const std::vector<CoalesceCounter> saved_counters = {
        {"nic_rx_usecs", "rx-usecs"},
        {"nic_rx_frames", "rx-frames"},
        {"nic_tx_usecs", "tx-usecs"},
        {"nic_tx_frames", "tx-frames"},
    };

    std::string iface = Storage::load("nic_iface");
    std::string adaptive_rx = Storage::exists("nic_adaptive_rx") ? Storage::load("nic_adaptive_rx") : "off";
    std::string adaptive_tx = Storage::exists("nic_adaptive_tx") ? Storage::load("nic_adaptive_tx") : "off";

    std::string adaptive_args = "adaptive-rx " + adaptive_rx + " adaptive-tx " + adaptive_tx;
    std::string full_args = adaptive_args;

    for (const auto& counter : saved_counters) {
        if (!Storage::exists(counter.storage_key)) continue;

        std::string value = Storage::load(counter.storage_key);
        if (value.empty()) continue;

        full_args += " " + std::string(counter.ethtool_name) + " " + value;
    }

    // A driver that moderates interrupts itself rejects explicit counters while the adaptive mode is on, so the flags alone are the fallback
    if (system(("ethtool -C " + iface + " " + full_args + " 2>/dev/null").c_str()) != 0) {
        system(("ethtool -C " + iface + " " + adaptive_args + " 2>/dev/null").c_str());
    }

    Storage::remove_file("nic_iface");
    Storage::remove_file("nic_adaptive_rx");
    Storage::remove_file("nic_adaptive_tx");

    for (const auto& counter : saved_counters) {
        Storage::remove_file(counter.storage_key);
    }
}


void Optimizer::apply_interface_qdisc(const std::string& iface) {
    if (iface.empty()) return;

    // CAKE classifies on the DS field, the same byte mac80211 turns into the WiFi access category, so a single mark drives both the wired queue and the radio contention parameters
    std::string cake_cmd = "tc qdisc replace dev " + iface + " root cake diffserv4 triple-isolate 2>/dev/null";

    if (system(cake_cmd.c_str()) != 0) {
        std::string fallback_cmd = "tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null";

        if (system(fallback_cmd.c_str()) != 0) {
            std::cerr << "[!!] could not set a low latency qdisc on " << iface << " (non-critical)\n";
            return;
        }
    }

    Storage::save("tc_iface", iface);
}


void Optimizer::revert_interface_qdisc(const std::string& iface) {
    if (iface.empty()) return;
    
    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    
    Storage::remove_file("tc_iface");
}


void Optimizer::apply_focus_priority(const std::string& iface) {
    if (iface.empty()) return;

    PriorityScheduler::apply_cgroup_hierarchy();
    Storage::save("focus_priority_iface", iface);
}


void Optimizer::revert_focus_priority(const std::string& iface) {
    if (iface.empty()) return;

    PriorityScheduler::revert_cgroup_hierarchy();
    Storage::remove_file("focus_priority_iface");
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
    system(("nmcli device reapply " + iface + " 2>/dev/null").c_str());
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
    system(("nmcli device reapply " + iface + " 2>/dev/null").c_str());
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

    // Validation and TLS setup each add round trips in front of the first byte of every new host
    f << "DNSSEC=no\n";
    f << "DNSOverTLS=no\n";

    system("systemctl restart systemd-resolved 2>/dev/null");
}


void Optimizer::revert_dns() {
    std::error_code ec;

    std::filesystem::remove("/etc/systemd/resolved.conf.d/betterconn.conf", ec);

    // The resolver holds the drop in contents in memory, so it has to be restarted even when the file was already removed
    system("systemctl restart systemd-resolved 2>/dev/null");
}


void Optimizer::write_persistence(const std::string& iface, LatencyProfile profile) {
    std::error_code ec;
    std::filesystem::create_directories("/etc/betterconn", ec);

    {
        std::ofstream f("/etc/modules-load.d/betterconn.conf");
        
        if (f) {
            f << "tcp_bbr\n";
            f << "sch_cake\n";
            f << "xt_DSCP\n";
            f << "xt_cgroup\n";
            f << "xt_length\n";
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
            f << "modprobe xt_DSCP 2>/dev/null\n";
            f << "modprobe xt_cgroup 2>/dev/null\n";
            f << "modprobe xt_length 2>/dev/null\n";
            f << "modprobe sch_cake 2>/dev/null\n";

            // xt_cgroup resolves the path when the rule is inserted, so the groups have to exist first
            for (const auto& command : PriorityScheduler::cgroup_setup_commands()) {
                f << command << "\n";
            }

            for (const auto& command : PacketMarking::idempotent_apply_commands()) {
                f << command << "\n";
            }

            if (!iface.empty()) {
                f << "ethtool -C " << iface << " " << ethtool_coalesce_args(profile) << " 2>/dev/null || true\n";
                f << "tc qdisc replace dev " << iface << " root cake diffserv4 triple-isolate 2>/dev/null"
                  << " || tc qdisc replace dev " << iface << " root fq_codel target 5ms interval 100ms 2>/dev/null || true\n";

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


void Optimizer::restore_pristine_baseline() {
    if (!SystemDefaults::persistence_present()) return;

    // Leftovers from an interrupted run keep the kernel tuned, so snapshotting now would store betterconn values as the machine baseline
    std::cerr << "[!!] leftover betterconn configuration found, restoring kernel defaults before taking a new baseline\n";

    PacketMarking::revert();
    PriorityScheduler::revert_cgroup_hierarchy();
    SystemDefaults::remove_persistence();
    SystemDefaults::restore_kernel_sysctl_defaults();
    SystemDefaults::reload_configured_sysctls();
}


void Optimizer::apply(const std::string& forced_iface) {
    if (is_active()) {
        throw std::runtime_error("betterconn is already active, run stop first");
    }
    restore_pristine_baseline();

    std::string iface = forced_iface.empty() ? detect_interface() : forced_iface;
    LatencyProfile profile = ProfileSelector::select(iface);

    load_kernel_modules();

    std::string avail;
    {
        std::ifstream f("/proc/sys/net/ipv4/tcp_available_congestion_control");
        std::getline(f, avail);
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

    apply_focus_priority(iface);
    apply_interface_qdisc(iface);
    apply_wifi_latency(iface);
    WifiAirtime::apply(iface);
    apply_nic_tuning(iface, profile);
    IrqAffinity::apply(iface);
    write_persistence(iface, profile);
    PacketMarking::apply();
    apply_dns();

    Storage::save("iface", iface);
    Storage::save("profile", ProfileSelector::to_string(profile));
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

    // Boot time reapplication is dropped before anything else, so a failure further down can never bring the tuning back on the next boot
    SystemDefaults::remove_persistence();

    std::string backup = Storage::load("sysctl_backup");
    std::istringstream ss(backup);
    std::string line;
    
    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // An empty backup value means the key does not exist on this kernel, so there is nothing to restore
        if (value.empty()) continue;
        
        if (!write_sysctl(key, value)) {
            std::cerr << "[!!] could not restore " << key << "\n";
        }
    }

    PacketMarking::revert();

    if (Storage::exists("focus_priority_iface")) {
        revert_focus_priority(Storage::load("focus_priority_iface"));
    }

    if (Storage::exists("tc_iface")) {
        revert_interface_qdisc(Storage::load("tc_iface"));
    }

    WifiAirtime::revert();
    IrqAffinity::revert();
    revert_wifi_latency();
    revert_nic_tuning();
    revert_dns();

    SystemDefaults::reload_configured_sysctls();

    Storage::save("state", "inactive");
    Storage::remove_file("sysctl_backup");
    Storage::remove_file("profile");
    Storage::remove_file("iface");
}


bool Optimizer::is_active() const {
    if (Storage::exists("sysctl_backup")) return true;

    if (!Storage::exists("state")) return false;
    
    return Storage::load("state") == "active";
}
}