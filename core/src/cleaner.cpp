// core/src/cleaner.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>





namespace betterconn {
void Cleaner::run() {
    if (Storage::exists("state") && Storage::load("state") == "active") {
        throw std::runtime_error("betterconn is active, run stop first");
    }

    system("systemctl stop betterconn.service 2>/dev/null");
    system("systemctl disable betterconn.service 2>/dev/null");
    system("systemctl reset-failed betterconn.service 2>/dev/null");
    system("pkill -TERM -f 'betterconn daemon' 2>/dev/null");

    std::error_code ec;

    std::filesystem::remove("/etc/systemd/system/betterconn.service", ec);
    std::filesystem::remove_all("/etc/betterconn", ec);
    std::filesystem::remove("/etc/sysctl.d/99-betterconn.conf", ec);
    std::filesystem::remove("/etc/modules-load.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/NetworkManager/conf.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/systemd/resolved.conf.d/betterconn.conf", ec);

    system("systemctl daemon-reload 2>/dev/null");
    system("nmcli general reload 2>/dev/null");
    system("systemctl restart systemd-resolved 2>/dev/null");

    std::filesystem::remove_all(Storage::dir(), ec);

    std::cout << "[OK] all betterconn files removed\n";
}
}