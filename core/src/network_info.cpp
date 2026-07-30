// core/src/network_info.cpp
#include "betterconn/colors.hpp"
#include "betterconn/network_info.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>





namespace betterconn {
std::string NetworkInfo::run_and_capture(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";

    std::string output;
    char buf[512];

    while (fgets(buf, sizeof(buf), p)) output += buf;

    pclose(p);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}

std::string NetworkInfo::nmcli_device_get(const std::string& field, const std::string& iface) {
    std::string cmd = "nmcli -t -g " + field + " device show " + iface + " 2>/dev/null";
    
    return run_and_capture(cmd);
}

std::string NetworkInfo::active_connection_name(const std::string& iface) {
    return nmcli_device_get("GENERAL.CONNECTION", iface);
}

std::string NetworkInfo::nmcli_get(const std::string& field, const std::string& connection_name) {
    std::string cmd = "nmcli -t -g " + field + " connection show \"" + connection_name + "\" 2>/dev/null";
    
    return run_and_capture(cmd);
}

bool NetworkInfo::connection_is_wifi(const std::string& connection_name) {
    std::string type = nmcli_get("connection.type", connection_name);

    return type == "802-11-wireless" || type == "wifi";
}

std::string NetworkInfo::connection_key_mgmt(const std::string& connection_name) {
    std::string cmd = "nmcli -t -g 802-11-wireless-security.key-mgmt connection show \"" + connection_name + "\" 2>/dev/null";

    return run_and_capture(cmd);
}

bool NetworkInfo::connection_has_8021x(const std::string& connection_name) {
    std::string cmd = "nmcli -t -g 802-1x.eap connection show \"" + connection_name + "\" 2>/dev/null";

    return !run_and_capture(cmd).empty();
}

std::string NetworkInfo::unescape_nmcli(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            ++i;
            result += value[i];
        } else {
            result += value[i];
        }
    }

    return result;
}

std::string NetworkInfo::first_value(const std::string& raw) {
    if (raw.empty()) return raw;

    size_t newline_pos = raw.find('\n');
    std::string first_line = (newline_pos == std::string::npos) ? raw : raw.substr(0, newline_pos);

    size_t bar_pos = first_line.find('|');
    std::string first_entry = (bar_pos == std::string::npos) ? first_line : first_line.substr(0, bar_pos);

    size_t space_pos = first_entry.find(' ');
    if (space_pos != std::string::npos) first_entry = first_entry.substr(0, space_pos);

    return first_entry;
}

void NetworkInfo::print_info(const std::string& iface) {
    if (iface.empty()) {
        std::cerr << CLR_LRED "[!!] could not detect an active network interface\n" CLR_RESET;

        return;
    }

    std::string connection_name = active_connection_name(iface);

    if (connection_name.empty()) {
        std::cerr << CLR_LRED "[!!] no active network connection on " << iface << "\n" CLR_RESET;

        return;
    }

    bool is_wifi = connection_is_wifi(connection_name);

    std::string type = nmcli_get("connection.type", connection_name);
    std::string state = nmcli_device_get("GENERAL.STATE", iface);
    std::string ipv4_addr = first_value(nmcli_device_get("IP4.ADDRESS", iface));
    std::string ipv4_gateway = nmcli_device_get("IP4.GATEWAY", iface);
    std::string ipv4_dns = first_value(nmcli_device_get("IP4.DNS", iface));
    std::string mac = nmcli_device_get("GENERAL.HWADDR", iface);
    std::string security = nmcli_get("802-11-wireless-security.key-mgmt", connection_name);

    std::cout << "\n" CLR_BOLD CLR_LWHITE "betterconn network info\n" CLR_RESET;
    std::cout << CLR_WHITE "-----------------------\n" CLR_RESET;
    std::cout << CLR_WHITE "Interface:          " CLR_RESET CLR_CYAN << iface << CLR_RESET "\n";
    std::cout << CLR_WHITE "Connection:         " CLR_RESET CLR_LGREEN << unescape_nmcli(connection_name) << CLR_RESET "\n";
    std::cout << CLR_WHITE "Type:               " CLR_RESET CLR_CYAN << (type.empty() ? "unknown" : unescape_nmcli(type)) << CLR_RESET "\n";
    std::cout << CLR_WHITE "State:              " CLR_RESET CLR_LCYAN << (state.empty() ? "unknown" : unescape_nmcli(state)) << CLR_RESET "\n";

    if (is_wifi) {
        std::string ssid = nmcli_get("802-11-wireless.ssid", connection_name);

        std::cout << CLR_WHITE "SSID:               " CLR_RESET CLR_LGREEN << (ssid.empty() ? "n/a" : unescape_nmcli(ssid)) << CLR_RESET "\n";
        std::cout << CLR_WHITE "Security:           " CLR_RESET CLR_LCYAN << (security.empty() ? "none" : unescape_nmcli(security)) << CLR_RESET "\n";
    }

    std::cout << CLR_WHITE "MAC address:        " CLR_RESET CLR_LCYAN << (mac.empty() ? "unknown" : unescape_nmcli(mac)) << CLR_RESET "\n";
    std::cout << CLR_WHITE "IPv4 address:       " CLR_RESET CLR_LCYAN << (ipv4_addr.empty() ? "unknown" : unescape_nmcli(ipv4_addr)) << CLR_RESET "\n";
    std::cout << CLR_WHITE "Gateway:            " CLR_RESET CLR_LCYAN << (ipv4_gateway.empty() ? "unknown" : unescape_nmcli(ipv4_gateway)) << CLR_RESET "\n";
    std::cout << CLR_WHITE "DNS:                " CLR_RESET CLR_LCYAN << (ipv4_dns.empty() ? "unknown" : unescape_nmcli(ipv4_dns)) << CLR_RESET "\n";
    std::cout << "\n";
}

void NetworkInfo::print_secrets(const std::string& iface) {
    if (iface.empty()) {
        std::cerr << CLR_LRED "[!!] could not detect an active network interface\n" CLR_RESET;

        return;
    }

    std::string connection_name = active_connection_name(iface);

    if (connection_name.empty()) {
        std::cerr << CLR_LRED "[!!] no active network connection on " << iface << "\n" CLR_RESET;

        return;
    }

    bool is_wifi = connection_is_wifi(connection_name);
    std::string key_mgmt = connection_key_mgmt(connection_name);
    bool has_8021x = connection_has_8021x(connection_name);

    std::cout << "\n" CLR_BOLD CLR_LWHITE "betterconn network secrets\n" CLR_RESET;
    std::cout << CLR_WHITE "--------------------------\n" CLR_RESET;
    std::cout << CLR_WHITE "Connection:         " CLR_RESET CLR_LGREEN << unescape_nmcli(connection_name) << CLR_RESET "\n";
    std::cout << CLR_WHITE "Type:               " CLR_RESET CLR_CYAN << unescape_nmcli(nmcli_get("connection.type", connection_name)) << CLR_RESET "\n";

    if (is_wifi) {
        std::string ssid = nmcli_get("802-11-wireless.ssid", connection_name);
        std::cout << CLR_WHITE "SSID:               " CLR_RESET CLR_LGREEN << (ssid.empty() ? "n/a" : unescape_nmcli(ssid)) << CLR_RESET "\n";
    }

    std::cout << CLR_WHITE "Security:           " CLR_RESET CLR_LCYAN << (key_mgmt.empty() ? "none" : unescape_nmcli(key_mgmt)) << CLR_RESET "\n";

    if (key_mgmt == "wpa-psk" || key_mgmt == "sae") {
        std::string psk_cmd = "nmcli -s -t -g 802-11-wireless-security.psk connection show \"" + connection_name + "\" 2>/dev/null";
        std::string psk = run_and_capture(psk_cmd);

        std::cout << CLR_WHITE "Password (PSK):     " CLR_RESET CLR_LGREEN << (psk.empty() ? "n/a" : unescape_nmcli(psk)) << CLR_RESET "\n";
    } else if (key_mgmt == "wpa-eap" || has_8021x) {
        std::string identity = nmcli_get("802-1x.identity", connection_name);
        std::string pass_cmd = "nmcli -s -t -g 802-1x.password connection show \"" + connection_name + "\" 2>/dev/null";
        std::string password = run_and_capture(pass_cmd);

        std::cout << CLR_WHITE "Identity:           " CLR_RESET CLR_LGREEN << (identity.empty() ? "n/a" : unescape_nmcli(identity)) << CLR_RESET "\n";
        std::cout << CLR_WHITE "Password (802.1X):  " CLR_RESET CLR_LGREEN << (password.empty() ? "n/a" : unescape_nmcli(password)) << CLR_RESET "\n";
    } else if (key_mgmt == "none" && is_wifi) {
        std::string wep_cmd = "nmcli -s -t -g 802-11-wireless-security.wep-key0 connection show \"" + connection_name + "\" 2>/dev/null";
        std::string wep_key = run_and_capture(wep_cmd);

        if (wep_key.empty()) {
            std::cout << CLR_YELLOW "[!!] network has no security (open network)\n" CLR_RESET;
        } else {
            std::cout << CLR_WHITE "WEP key:            " CLR_RESET CLR_LGREEN << unescape_nmcli(wep_key) << CLR_RESET "\n";
        }
    } else {
        std::cout << CLR_YELLOW "[!!] this connection has no stored secret to display\n" CLR_RESET;
    }

    std::cout << "\n";
}
void NetworkInfo::force_network_on(const std::string& iface) {
    std::cout << CLR_YELLOW "[..] forcing network interface/NetworkManager on...\n" CLR_RESET;

    system("rfkill unblock wifi 2>/dev/null");
    system("rfkill unblock all 2>/dev/null");

    system("systemctl unmask NetworkManager 2>/dev/null");
    system("systemctl enable NetworkManager 2>/dev/null");
    system("systemctl start NetworkManager 2>/dev/null");
    system("systemctl restart NetworkManager 2>/dev/null");

    system("nmcli networking on 2>/dev/null");
    system("nmcli radio wifi on 2>/dev/null");
    system("nmcli radio all on 2>/dev/null");

    if (!iface.empty()) {
        system(("ip link set dev " + iface + " up 2>/dev/null").c_str());
        system(("nmcli device set " + iface + " managed yes 2>/dev/null").c_str());
        system(("nmcli device connect " + iface + " 2>/dev/null").c_str());
    }

    std::cout << CLR_LGREEN "[OK] attempted to force network on\n" CLR_RESET;
}

void NetworkInfo::force_network_off(const std::string& iface) {
    std::cout << CLR_YELLOW "[..] forcing network interface/NetworkManager off...\n" CLR_RESET;

    if (!iface.empty()) {
        system(("nmcli device disconnect " + iface + " 2>/dev/null").c_str());
        system(("ip link set dev " + iface + " down 2>/dev/null").c_str());
    }

    system("nmcli radio wifi off 2>/dev/null");
    system("nmcli radio all off 2>/dev/null");
    system("nmcli networking off 2>/dev/null");

    system("systemctl stop NetworkManager 2>/dev/null");

    system("rfkill block wifi 2>/dev/null");

    std::cout << CLR_LGREEN "[OK] attempted to force network off\n" CLR_RESET;
}


void NetworkInfo::print_help() {
    std::cout << "\n" CLR_BOLD CLR_LWHITE "betterconn network" CLR_RESET " - Active network connection info\n";
    std::cout << "\n" CLR_BOLD "Usage:\n" CLR_RESET;
    std::cout << "  " CLR_CYAN "betterconn network" CLR_RESET "                      Show info for the currently active network (default)\n";
    std::cout << "  " CLR_CYAN "betterconn network --info" CLR_RESET "               Show info for the currently active network\n";
    std::cout << "  " CLR_CYAN "betterconn network --interface on" CLR_RESET "     Force-enable the network interface / NetworkManager\n";
    std::cout << "  " CLR_CYAN "betterconn network --interface off" CLR_RESET "    Force-disable the network interface / NetworkManager\n";
    std::cout << "  " CLR_CYAN "betterconn network --secrets" CLR_RESET "            Show the password of the currently active network\n";
    std::cout << "  " CLR_CYAN "betterconn network -h, --help" CLR_RESET "           Show this message\n";
    std::cout << "\n";
    std::cout << CLR_WHITE "Notes:\n" CLR_RESET;
    std::cout << CLR_WHITE "  This command only reads the network you are currently connected to.\n";
    std::cout << "  It never reads or lists any other saved network profile.\n" CLR_RESET;
    std::cout << "\n";
}
}