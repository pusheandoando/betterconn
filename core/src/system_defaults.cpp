// core/src/system_defaults.cpp
#include "betterconn/system_defaults.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <filesystem>





namespace betterconn {
namespace {
const char* kBetterconnEtcDir = "/etc/betterconn";


const std::array<const char*, 5> kOptimizerModules = {
    "xt_DSCP",
    "xt_cgroup",
    "xt_length",
    "sch_cake",
    "tcp_bbr"
};
}


std::vector<std::string> SystemDefaults::persistence_paths() {
    return {
        "/etc/systemd/system/betterconn.service",
        "/etc/sysctl.d/99-betterconn.conf",
        "/etc/modules-load.d/betterconn.conf",
        "/etc/NetworkManager/conf.d/betterconn.conf",
        "/etc/systemd/resolved.conf.d/betterconn.conf",
    };
}


bool SystemDefaults::persistence_present() {
    for (const auto& path : persistence_paths()) {
        if (std::filesystem::exists(path)) return true;
    }

    return std::filesystem::exists(kBetterconnEtcDir);
}


void SystemDefaults::remove_persistence() {
    system("systemctl stop betterconn.service 2>/dev/null");
    system("systemctl disable betterconn.service 2>/dev/null");
    system("systemctl reset-failed betterconn.service 2>/dev/null");

    std::error_code ec;

    for (const auto& path : persistence_paths()) {
        std::filesystem::remove(path, ec);
    }

    std::filesystem::remove_all(kBetterconnEtcDir, ec);

    system("systemctl daemon-reload 2>/dev/null");
}


std::vector<SysctlDefault> SystemDefaults::kernel_defaults() {
    return {
        {"net.core.default_qdisc", "pfifo_fast"},

        {"net.ipv4.tcp_congestion_control", "cubic"},

        {"net.core.rmem_max", "212992"},
        {"net.core.wmem_max", "212992"},
        {"net.core.rmem_default", "212992"},
        {"net.core.wmem_default", "212992"},

        {"net.ipv4.tcp_rmem", "4096 131072 6291456"},
        {"net.ipv4.tcp_wmem", "4096 16384 4194304"},

        {"net.ipv4.udp_rmem_min", "4096"},
        {"net.ipv4.udp_wmem_min", "4096"},

        {"net.core.netdev_max_backlog", "1000"},
        {"net.core.netdev_budget", "300"},
        {"net.core.netdev_budget_usecs", "2000"},

        {"net.ipv4.tcp_fastopen", "1"},
        {"net.ipv4.tcp_window_scaling", "1"},
        {"net.ipv4.tcp_timestamps", "1"},
        {"net.ipv4.tcp_sack", "1"},
        {"net.ipv4.tcp_ecn", "2"},
        {"net.ipv4.tcp_fin_timeout", "60"},
        {"net.ipv4.tcp_tw_reuse", "2"},
        {"net.ipv4.tcp_autocorking", "1"},
        {"net.ipv4.tcp_notsent_lowat", "4294967295"},
        {"net.ipv4.tcp_slow_start_after_idle", "1"},
        {"net.ipv4.tcp_mtu_probing", "0"},
        {"net.ipv4.ip_local_port_range", "32768 60999"},
        {"net.ipv4.tcp_rto_min_us", "200000"},
        {"net.ipv4.tcp_keepalive_time", "7200"},
        {"net.ipv4.tcp_keepalive_intvl", "75"},
        {"net.ipv4.tcp_keepalive_probes", "9"},

        {"net.ipv4.tcp_moderate_rcvbuf", "1"},
        {"net.ipv4.tcp_recovery", "1"},
        {"net.ipv4.tcp_frto", "2"},
        {"net.ipv4.tcp_thin_linear_timeouts", "0"},
        {"net.ipv4.tcp_no_metrics_save", "0"},
    };
}


void SystemDefaults::write_sysctl(const std::string& key, const std::string& value) {
    std::string path = "/proc/sys/";

    for (char c : key) path += (c == '.') ? '/' : c;

    std::ofstream f(path);

    if (f) f << value << "\n";
}


void SystemDefaults::restore_kernel_sysctl_defaults() {
    for (const auto& entry : kernel_defaults()) {
        write_sysctl(entry.key, entry.value);
    }
}


bool SystemDefaults::command_available(const std::string& command) {
    return system(("command -v " + command + " >/dev/null 2>&1").c_str()) == 0;
}


void SystemDefaults::reload_configured_sysctls() {
    // The distribution owns every value betterconn did not create, so its own configuration has to be the last writer
    if (system("systemctl restart systemd-sysctl 2>/dev/null") == 0) return;

    if (command_available("sysctl")) {
        system("sysctl --system >/dev/null 2>&1");
    }
}


void SystemDefaults::unload_optimizer_modules() {
    // A module still pinned by a live socket refuses to go, which is harmless because nothing loads it at boot once the persistence is gone
    for (const char* module : kOptimizerModules) {
        system((std::string("modprobe -r ") + module + " 2>/dev/null").c_str());
    }
}


void SystemDefaults::regenerate_initramfs() {
    if (command_available("update-initramfs")) {
        system("update-initramfs -u >/dev/null 2>&1");
        return;
    }

    if (command_available("dracut")) {
        system("dracut --force >/dev/null 2>&1");
    }
}
}