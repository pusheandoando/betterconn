// core/src/irq_affinity.cpp
#include "betterconn/irq_affinity.hpp"
#include "betterconn/storage.hpp"

#include <thread>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>





namespace betterconn {
int IrqAffinity::detect_cpu_count() {
    unsigned int count = std::thread::hardware_concurrency();

    return count > 0 ? static_cast<int>(count) : 1;
}


std::vector<int> IrqAffinity::find_interface_irqs(const std::string& iface) {
    std::vector<int> irqs;
    std::ifstream f("/proc/interrupts");
    if (!f) return irqs;

    std::string line;

    while (std::getline(f, line)) {
        if (line.find(iface) == std::string::npos) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::istringstream ss(line.substr(0, colon));
        int irq;

        if (ss >> irq) irqs.push_back(irq);
    }

    return irqs;
}


bool IrqAffinity::irqbalance_active() {
    return system("systemctl is-active --quiet irqbalance 2>/dev/null") == 0;
}


void IrqAffinity::stop_irqbalance() {
    if (!irqbalance_active()) return;

    Storage::save("irqbalance_was_active", "1");
    system("systemctl stop irqbalance 2>/dev/null");
}


void IrqAffinity::restore_irqbalance() {
    if (!Storage::exists("irqbalance_was_active")) return;

    system("systemctl start irqbalance 2>/dev/null");
    Storage::remove_file("irqbalance_was_active");
}


std::string IrqAffinity::cpu_mask_hex(int cpu) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%x", 1 << cpu);

    return std::string(buf);
}


std::string IrqAffinity::all_cpus_mask_hex(int cpu_count) {
    unsigned long mask = 0;

    for (int i = 0; i < cpu_count && i < 63; ++i) {
        mask |= (1UL << i);
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lx", mask);

    return std::string(buf);
}


int IrqAffinity::read_irq_affinity_cpu(int irq) {
    std::ifstream f("/proc/irq/" + std::to_string(irq) + "/smp_affinity_list");
    if (!f) return -1;

    std::string value;
    std::getline(f, value);

    auto comma = value.find(',');
    auto dash = value.find('-');
    std::string first_token = value.substr(0, std::min(comma, dash));

    try {
        return std::stoi(first_token);
    } catch (...) {
        return -1;
    }
}


void IrqAffinity::pin_irq_to_cpu(int irq, int cpu) {
    std::string path = "/proc/irq/" + std::to_string(irq) + "/smp_affinity_list";

    int previous_cpu = read_irq_affinity_cpu(irq);
    if (previous_cpu >= 0) {
        Storage::save("irq_prev_" + std::to_string(irq), std::to_string(previous_cpu));
    }

    std::ofstream f(path);
    if (!f) {
        std::cerr << "[!!] could not set affinity for irq " << irq << " (non-critical)\n";
        return;
    }

    f << cpu;
}


std::vector<std::string> IrqAffinity::list_queue_dirs(const std::string& iface, const std::string& kind) {
    std::vector<std::string> dirs;
    std::string queues_path = "/sys/class/net/" + iface + "/queues";

    if (!std::filesystem::exists(queues_path)) return dirs;

    for (const auto& entry : std::filesystem::directory_iterator(queues_path)) {
        std::string name = entry.path().filename().string();

        if (name.rfind(kind, 0) == 0) dirs.push_back(name);
    }

    std::sort(dirs.begin(), dirs.end());

    return dirs;
}


void IrqAffinity::apply_rps_xps_fallback(const std::string& iface, int cpu_count) {
    std::string mask = all_cpus_mask_hex(cpu_count);

    for (const auto& rx_dir : list_queue_dirs(iface, "rx-")) {
        std::string path = "/sys/class/net/" + iface + "/queues/" + rx_dir + "/rps_cpus";
        std::ofstream f(path);

        if (f) f << mask;
    }

    for (const auto& tx_dir : list_queue_dirs(iface, "tx-")) {
        std::string path = "/sys/class/net/" + iface + "/queues/" + tx_dir + "/xps_cpus";
        std::ofstream f(path);

        if (f) f << mask;
    }

    Storage::save("rps_xps_iface", iface);
}


void IrqAffinity::revert_rps_xps_fallback(const std::string& iface) {
    for (const auto& rx_dir : list_queue_dirs(iface, "rx-")) {
        std::string path = "/sys/class/net/" + iface + "/queues/" + rx_dir + "/rps_cpus";
        std::ofstream f(path);

        if (f) f << "0";
    }

    for (const auto& tx_dir : list_queue_dirs(iface, "tx-")) {
        std::string path = "/sys/class/net/" + iface + "/queues/" + tx_dir + "/xps_cpus";
        std::ofstream f(path);

        if (f) f << "0";
    }

    Storage::remove_file("rps_xps_iface");
}


void IrqAffinity::apply(const std::string& iface) {
    if (iface.empty()) return;

    std::vector<int> irqs = find_interface_irqs(iface);
    int cpu_count = detect_cpu_count();

    apply_rps_xps_fallback(iface, cpu_count);

    if (irqs.empty()) {
        return;
    }

    stop_irqbalance();

    std::ostringstream irq_list;
    for (size_t i = 0; i < irqs.size(); ++i) {
        int target_cpu = static_cast<int>(i) % cpu_count;

        pin_irq_to_cpu(irqs[i], target_cpu);
        irq_list << irqs[i] << (i + 1 < irqs.size() ? "," : "");
    }

    Storage::save("irq_affinity_list", irq_list.str());
    Storage::save("irq_affinity_iface", iface);
}


void IrqAffinity::revert() {
    if (Storage::exists("rps_xps_iface")) {
        revert_rps_xps_fallback(Storage::load("rps_xps_iface"));
    }

    if (!Storage::exists("irq_affinity_list")) return;

    std::string raw = Storage::load("irq_affinity_list");
    std::istringstream ss(raw);
    std::string token;

    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;

        std::string prev_key = "irq_prev_" + token;

        if (Storage::exists(prev_key)) {
            std::ofstream f("/proc/irq/" + token + "/smp_affinity_list");

            if (f) f << Storage::load(prev_key);

            Storage::remove_file(prev_key);
        }
    }

    restore_irqbalance();

    Storage::remove_file("irq_affinity_list");
    Storage::remove_file("irq_affinity_iface");
}
}