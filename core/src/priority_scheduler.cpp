// core/src/priority_scheduler.cpp
#include "betterconn/priority_scheduler.hpp"
#include "betterconn/proc_activity.hpp"

#include <cmath>
#include <array>
#include <vector>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>





static constexpr int kStaleEntryPruneEveryTicks = 40;

namespace betterconn {
bool PriorityScheduler::cgroup_v2_mounted() {
    return std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers");
}

PriorityTier PriorityScheduler::tier_for_decay(double decay_factor) {
    if (decay_factor >= 0.7) return PriorityTier::Warm;
    if (decay_factor >= 0.3) return PriorityTier::Cool;

    return PriorityTier::Cold;
}

bool PriorityScheduler::tier_boundary_crossed_with_margin(double decay_factor, PriorityTier current_tier, PriorityTier proposed_tier) {
    if (current_tier == proposed_tier) return false;

    static constexpr double kHysteresisMargin = 0.05;

    if (proposed_tier > current_tier) {
        double threshold = current_tier == PriorityTier::Cold ? 0.3 : 0.7;

        return decay_factor >= threshold + kHysteresisMargin;
    }

    double threshold = current_tier == PriorityTier::Warm ? 0.7 : 0.3;

    return decay_factor < threshold - kHysteresisMargin;
}

bool PriorityScheduler::tier_mass_shifted_significantly(const std::vector<double>& previous, const std::vector<double>& current) {
    if (previous.size() != current.size()) return true;

    static constexpr double kRelativeChangeThreshold = 0.15;
    static constexpr double kAbsoluteChangeFloor = 5.0;

    for (size_t i = 0; i < previous.size(); ++i) {
        if (previous[i] < 0.0) return true;

        double delta = std::abs(current[i] - previous[i]);
        double reference = std::max(previous[i], kAbsoluteChangeFloor);

        if (delta / reference >= kRelativeChangeThreshold) return true;
    }

    return false;
}

const char* PriorityScheduler::tier_cgroup_name(PriorityTier tier) {
    switch (tier) {
        case PriorityTier::Hot: return "hot";
        case PriorityTier::Warm: return "warm";
        case PriorityTier::Cool: return "cool";
        case PriorityTier::Cold:
        default: return "cold";
    }
}

int PriorityScheduler::tier_fw_mark(PriorityTier tier) {
    switch (tier) {
        case PriorityTier::Hot: return kHotFwMark;
        case PriorityTier::Warm: return kWarmFwMark;
        case PriorityTier::Cool: return kCoolFwMark;
        case PriorityTier::Cold:
        default: return kColdFwMark;
    }
}

void PriorityScheduler::apply_cgroup_and_marking() {
    if (!cgroup_v2_mounted()) {
        std::cerr << "[!!] cgroup v2 not mounted, priority scheduling disabled\n";
        return;
    }

    std::error_code ec;

    const std::array<PriorityTier, 4> tiers = {
        PriorityTier::Hot, PriorityTier::Warm, PriorityTier::Cool, PriorityTier::Cold
    };

    for (PriorityTier tier : tiers) {
        std::string tier_path = std::string(kCgroupBasePath) + "/" + tier_cgroup_name(tier);
        std::filesystem::create_directories(tier_path, ec);

        if (ec) {
            std::cerr << "[!!] could not create priority cgroup " << tier_path << " (non-critical)\n";
            
            continue;
        }

        char mark_buf[16];
        std::snprintf(mark_buf, sizeof(mark_buf), "0x%x", tier_fw_mark(tier));

        std::string check_cmd = "iptables -t mangle -C OUTPUT -m cgroup --path betterconn_priority/" + std::string(tier_cgroup_name(tier)) + " -j MARK --set-mark " + mark_buf + " 2>/dev/null";
        std::string add_cmd = "iptables -t mangle -A OUTPUT -m cgroup --path betterconn_priority/" + std::string(tier_cgroup_name(tier)) + " -j MARK --set-mark " + mark_buf + " 2>/dev/null";

        if (system(check_cmd.c_str()) != 0) {
            if (system(add_cmd.c_str()) != 0) {
                std::cerr << "[!!] could not add priority marking rule for " << tier_cgroup_name(tier) << " (non-critical)\n";
            }
        }
    }
}

void PriorityScheduler::revert_cgroup_and_marking() {
    const std::array<PriorityTier, 4> tiers = {
        PriorityTier::Hot, PriorityTier::Warm, PriorityTier::Cool, PriorityTier::Cold
    };

    for (PriorityTier tier : tiers) {
        char mark_buf[16];
        std::snprintf(mark_buf, sizeof(mark_buf), "0x%x", tier_fw_mark(tier));

        std::string del_cmd = "iptables -t mangle -D OUTPUT -m cgroup --path betterconn_priority/" + std::string(tier_cgroup_name(tier)) + " -j MARK --set-mark " + mark_buf + " 2>/dev/null";
        system(del_cmd.c_str());
    }

    std::error_code ec;
    std::filesystem::remove_all(kCgroupBasePath, ec);
}

void PriorityScheduler::move_pid_to_tier(int pid, PriorityTier tier) {
    if (pid <= 0) return;

    std::string tier_path = std::string(kCgroupBasePath) + "/" + tier_cgroup_name(tier);
    if (!std::filesystem::exists(tier_path)) return;

    std::ofstream f(tier_path + "/cgroup.procs");
    if (!f) return;

    f << pid;
}

void PriorityScheduler::apply_qdisc_hierarchy(const std::string& iface) {
    if (iface.empty()) return;

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());

    bool ok = true;
    ok &= system(("tc qdisc replace dev " + iface + " root handle 1: htb default 40 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1: classid 1:1 htb rate 1000mbit ceil 1000mbit 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kHotClassId) + " htb rate 550mbit ceil 1000mbit prio 1 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kWarmClassId) + " htb rate 300mbit ceil 1000mbit prio 2 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kCoolClassId) + " htb rate 100mbit ceil 800mbit prio 3 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kColdClassId) + " htb rate 50mbit ceil 600mbit prio 4 2>/dev/null").c_str()) == 0;

    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kHotClassId) + " handle 10: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;
    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kWarmClassId) + " handle 20: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;
    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kCoolClassId) + " handle 30: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;
    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kColdClassId) + " handle 40: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;

    const std::array<PriorityTier, 4> tiers = {
        PriorityTier::Hot, PriorityTier::Warm, PriorityTier::Cool, PriorityTier::Cold
    };

    const std::array<const char*, 4> classids = {
        kHotClassId, kWarmClassId, kCoolClassId, kColdClassId
    };

    for (size_t i = 0; i < tiers.size(); ++i) {
        char mark_buf[16];
        std::snprintf(mark_buf, sizeof(mark_buf), "0x%x", tier_fw_mark(tiers[i]));

        ok &= system(("tc filter add dev " + iface + " parent 1: protocol ip prio 1 handle " + mark_buf + " fw flowid " + std::string(classids[i]) + " 2>/dev/null").c_str()) == 0;
    }

    if (!ok) {
        std::cerr << "[!!] could not fully apply priority qdisc hierarchy on " << iface << " (non-critical)\n";
    }
}

void PriorityScheduler::revert_qdisc_hierarchy(const std::string& iface) {
    if (iface.empty()) return;

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    system(("tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null").c_str());
}

void PriorityScheduler::rebalance_tier_bandwidth(const std::string& iface, const std::vector<double>& tier_mass) {
    double total_mass = tier_mass[0] + tier_mass[1] + tier_mass[2] + tier_mass[3];
    if (total_mass <= 0.0) return;

    const std::array<const char*, 4> classids = {
        kHotClassId, kWarmClassId, kCoolClassId, kColdClassId
    };

    const std::array<double, 4> min_share = {0.25, 0.10, 0.05, 0.02};

    for (size_t i = 0; i < classids.size(); ++i) {
        double proportional_share = tier_mass[i] / total_mass;
        double effective_share = std::max(proportional_share, min_share[i]);

        int rate_mbit = static_cast<int>(effective_share * 1000.0);
        rate_mbit = std::max(rate_mbit, 10);

        std::string cmd = "tc class change dev " + iface + " parent 1:1 classid " + std::string(classids[i]) + " htb rate " + std::to_string(rate_mbit) + "mbit ceil 1000mbit 2>/dev/null";

        system(cmd.c_str());
    }
}

void PriorityScheduler::poll_loop(const std::string& iface, std::atomic<bool>& running) {
    WindowFocusBackend backend = WindowFocusDetector::detect_available_backend();
    if (backend == WindowFocusBackend::None) return;
    if (!cgroup_v2_mounted()) return;

    FocusDecayTracker focus_tracker;
    ContextPersistence context_persistence;
    std::unordered_map<int, PriorityTier> current_tier;
    std::unordered_map<int, int> ticks_in_tier;
    std::vector<double> last_applied_tier_mass(4, -1.0);
    int ticks_since_last_rebalance = kMinRebalanceIntervalTicks;

    int tick_count = 0;

    while (running.load(std::memory_order_relaxed)) {
        int pid_under_cursor = WindowFocusDetector::get_pid_under_cursor(backend);

        if (pid_under_cursor > 0) {
            focus_tracker.mark_focused(pid_under_cursor);
            context_persistence.record_focus_change(pid_under_cursor);

            if (context_persistence.has_companion(pid_under_cursor)) {
                int companion = context_persistence.companion_pid(pid_under_cursor);

                if (companion > 0) {
                    focus_tracker.mark_focused(companion);
                }
            }
        }

        if (tick_count % kNetworkRescanEveryTicks == 0) {
            for (int pid : focus_tracker.tracked_pids()) {
                auto activity = ProcessActivity::read_net_activity(pid);
                bool has_activity = activity.has_established_tcp || activity.udp_socket_count > 0;

                focus_tracker.update_network_activity(pid, has_activity);
            }
        }

        std::vector<double> tier_mass(4, 0.0);

        for (int pid : focus_tracker.tracked_pids()) {
            bool currently_focused = focus_tracker.is_currently_focused(pid);
            bool has_network_activity = focus_tracker.has_network_activity(pid);

            if (!currently_focused && !has_network_activity) {
                continue;
            }

            double decay_factor = focus_tracker.decay_factor_for(pid);
            PriorityTier proposed_tier = tier_for_decay(decay_factor);

            auto tier_it = current_tier.find(pid);
            PriorityTier effective_tier = proposed_tier;
            bool tier_changed = true;

            if (currently_focused) {
                effective_tier = PriorityTier::Hot;
                ticks_in_tier[pid] = 0;
                tier_changed = (tier_it == current_tier.end() || tier_it->second != PriorityTier::Hot);
            } else if (tier_it != current_tier.end()) {
                effective_tier = tier_it->second;
                int ticks_held = ticks_in_tier[pid];

                bool boundary_crossed_with_margin = tier_boundary_crossed_with_margin(decay_factor, tier_it->second, proposed_tier);

                if (boundary_crossed_with_margin && ticks_held >= kMinTierDwellTicks) {
                    effective_tier = proposed_tier;
                    ticks_in_tier[pid] = 0;
                    tier_changed = (effective_tier != tier_it->second);
                } else {
                    ticks_in_tier[pid] = ticks_held + 1;
                    tier_changed = false;
                }
            } else {
                ticks_in_tier[pid] = 0;
            }

            current_tier[pid] = effective_tier;

            if (tier_changed) {
                move_pid_to_tier(pid, effective_tier);
            }

            size_t tier_index = static_cast<size_t>(effective_tier == PriorityTier::Hot ? 0
                : effective_tier == PriorityTier::Warm ? 1
                : effective_tier == PriorityTier::Cool ? 2
                : 3);

            double mass_contribution = currently_focused ? 100.0 : decay_factor * 100.0;
            tier_mass[tier_index] += mass_contribution;
        }

        ++ticks_since_last_rebalance;

        if (ticks_since_last_rebalance >= kMinRebalanceIntervalTicks
            && tier_mass_shifted_significantly(last_applied_tier_mass, tier_mass)) {
            rebalance_tier_bandwidth(iface, tier_mass);
            last_applied_tier_mass = tier_mass;
            ticks_since_last_rebalance = 0;
        }

        if (tick_count % kStaleEntryPruneEveryTicks == 0) {
            focus_tracker.forget_stale_entries();
        }

        ++tick_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}

void PriorityScheduler::start(const std::string& iface) {
    running_.store(true, std::memory_order_relaxed);

    poll_thread_ = std::thread([iface, this]() {
        poll_loop(iface, running_);
    });
}

void PriorityScheduler::stop() {
    running_.store(false, std::memory_order_relaxed);

    if (poll_thread_.joinable()) poll_thread_.join();
}
}