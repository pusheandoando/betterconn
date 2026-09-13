// core/include/betterconn/irq_affinity.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
class IrqAffinity {
public:
    static void apply(const std::string& iface);
    static void revert();

private:
    static std::vector<int> find_interface_irqs(const std::string& iface);
    static int detect_cpu_count();
    static bool irqbalance_active();
    static void stop_irqbalance();
    static void restore_irqbalance();
    static void pin_irq_to_cpu(int irq, int cpu);
    static int read_irq_affinity_cpu(int irq);
    static void apply_rps_xps_fallback(const std::string& iface, int cpu_count);
    static void revert_rps_xps_fallback(const std::string& iface);
    static std::vector<std::string> list_queue_dirs(const std::string& iface, const std::string& kind);
    static std::string cpu_mask_hex(int cpu);
    static std::string all_cpus_mask_hex(int cpu_count);
};
}