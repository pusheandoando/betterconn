// core/include/betterconn/optimizer.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
struct SysctlParam {
    std::string key;
    std::string value;
};

class Optimizer {
public:
    void apply();
    void revert();
    bool is_active() const;

private:
    static std::string sysctl_path(const std::string& key);
    static std::string read_sysctl(const std::string& key);
    static bool write_sysctl(const std::string& key, const std::string& value);
    static void load_bbr_module();
    static void apply_iptables();
    static void revert_iptables();
    static std::vector<std::string> iptables_add_rules();
    static void write_persistence(const std::string& iface);
    static void remove_persistence();
    static std::string detect_interface();
    static void apply_nic_tuning(const std::string& iface);
    static void revert_nic_tuning();
    static bool is_wifi(const std::string& iface);
    static void apply_interface_qdisc(const std::string& iface);
    static void revert_interface_qdisc(const std::string& iface);
    static void apply_wifi_latency(const std::string& iface);
    static void revert_wifi_latency();
    static void apply_dns();
    static void revert_dns();
};
}