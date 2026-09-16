// core/include/betterconn/optimizer.hpp
#pragma once

#include "betterconn/profile_selector.hpp"

#include <string>
#include <vector>





namespace betterconn {
struct SysctlParam {
    std::string key;
    std::string value;
};


class Optimizer {
public:
    void apply(const std::string& forced_iface="");
    void revert();
    bool is_active() const;

private:
    static std::string sysctl_path(const std::string& key);
    static std::string read_sysctl(const std::string& key);
    static bool write_sysctl(const std::string& key, const std::string& value);
    static void load_kernel_modules();
    static void write_persistence(const std::string& iface, LatencyProfile profile);
    static void restore_pristine_baseline();
    static std::string detect_interface();
    static void apply_nic_tuning(const std::string& iface, LatencyProfile profile);
    static void revert_nic_tuning();
    static void save_nic_coalesce_state(const std::string& iface);
    static std::string coalesce_field(const std::string& output, const std::string& key);
    static bool is_wifi(const std::string& iface);
    static void apply_interface_qdisc(const std::string& iface);
    static void revert_interface_qdisc(const std::string& iface);
    static void apply_wifi_latency(const std::string& iface);
    static void revert_wifi_latency();
    static void apply_dns();
    static void revert_dns();
    static void apply_focus_priority(const std::string& iface);
    static void revert_focus_priority(const std::string& iface);
    static std::string ethtool_coalesce_args(LatencyProfile profile);
};
}