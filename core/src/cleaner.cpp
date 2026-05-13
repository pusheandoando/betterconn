// core/src/cleaner.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/colors.hpp"
#include "betterconn/storage.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>





namespace betterconn {
void Cleaner::run() {
    std::cout << CLR_YELLOW "[..] stopping betterconn service...\n" CLR_RESET;
    system("systemctl stop betterconn.service 2>/dev/null");
    system("systemctl disable betterconn.service 2>/dev/null");
    system("systemctl reset-failed betterconn.service 2>/dev/null");
    system("pkill -TERM -f 'betterconn daemon' 2>/dev/null");

    std::cout << CLR_YELLOW "[..] removing system files...\n" CLR_RESET;

    std::error_code ec;

    std::filesystem::remove("/etc/systemd/system/betterconn.service", ec);
    std::filesystem::remove_all("/etc/betterconn", ec);
    std::filesystem::remove("/etc/sysctl.d/99-betterconn.conf", ec);
    std::filesystem::remove("/etc/modules-load.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/NetworkManager/conf.d/betterconn.conf", ec);
    std::filesystem::remove("/etc/systemd/resolved.conf.d/betterconn.conf", ec);

    std::cout << CLR_YELLOW "[..] reloading system services...\n" CLR_RESET;
    system("systemctl daemon-reload 2>/dev/null");
    system("nmcli general reload 2>/dev/null");
    system("systemctl restart systemd-resolved 2>/dev/null");

    std::filesystem::remove_all(Storage::dir(), ec);

    std::cout << CLR_LGREEN "[OK] all betterconn files removed\n" CLR_RESET;
}
}