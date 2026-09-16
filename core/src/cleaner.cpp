// core/src/cleaner.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/colors.hpp"
#include "betterconn/storage.hpp"
#include "betterconn/packet_marking.hpp"
#include "betterconn/system_defaults.hpp"
#include "betterconn/priority_scheduler.hpp"

#include <cstdlib>
#include <iostream>
#include <filesystem>





namespace betterconn {
void Cleaner::run() {
    // Residue with no usable backup means an earlier run never restored anything, so the kernel is still carrying the tuning
    bool orphaned_tuning = SystemDefaults::persistence_present() && !Storage::exists("sysctl_backup");

    std::cout << CLR_YELLOW "[..] stopping betterconn service...\n" CLR_RESET;

    system("pkill -TERM -f 'betterconn daemon' 2>/dev/null");

    std::cout << CLR_YELLOW "[..] removing traffic marking and priority groups...\n" CLR_RESET;

    PacketMarking::revert();
    PriorityScheduler::revert_cgroup_hierarchy();

    std::cout << CLR_YELLOW "[..] removing system files...\n" CLR_RESET;

    SystemDefaults::remove_persistence();

    if (orphaned_tuning) {
        std::cout << CLR_YELLOW "[..] restoring kernel defaults...\n" CLR_RESET;

        SystemDefaults::restore_kernel_sysctl_defaults();
    }

    std::cout << CLR_YELLOW "[..] reloading system services...\n" CLR_RESET;

    SystemDefaults::reload_configured_sysctls();
    system("nmcli general reload 2>/dev/null");
    system("systemctl restart systemd-resolved 2>/dev/null");

    SystemDefaults::unload_optimizer_modules();

    std::cout << CLR_YELLOW "[..] regenerating initramfs...\n" CLR_RESET;

    SystemDefaults::regenerate_initramfs();

    std::error_code ec;

    std::filesystem::remove_all(Storage::dir(), ec);

    std::cout << CLR_LGREEN "[OK] all betterconn files removed\n" CLR_RESET;
}
}