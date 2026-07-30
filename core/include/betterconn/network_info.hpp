// core/include/betterconn/network_info.hpp
#pragma once

#include <string>





namespace betterconn {
class NetworkInfo {
public:
    static void print_info(const std::string& iface);
    static void print_secrets(const std::string& iface);
    static void print_help();
    static void force_network_on(const std::string& iface);
    static void force_network_off(const std::string& iface);

private:
    static std::string active_connection_name(const std::string& iface);
    static std::string nmcli_get(const std::string& field, const std::string& connection_name);
    static std::string nmcli_device_get(const std::string& field, const std::string& iface);
    static bool connection_is_wifi(const std::string& connection_name);
    static std::string run_and_capture(const std::string& cmd);
    static std::string unescape_nmcli(const std::string& value);
    static std::string first_value(const std::string& raw);
    static std::string connection_key_mgmt(const std::string& connection_name);
    static bool connection_has_8021x(const std::string& connection_name);
};
}