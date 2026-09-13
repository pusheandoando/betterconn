// core/src/priority_scheduler.cpp
#include "betterconn/priority_scheduler.hpp"
#include "betterconn/proc_activity.hpp"

#include <array>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>





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

    // fwmark 0xff has no trailing unset bits, so CAKE uses the mark value directly as the tin index (1=Cold .. 4=Hot under diffserv4)
    std::string cmd = "tc qdisc replace dev " + iface + " root handle 1: cake diffserv4 triple-isolate fwmark 0xff 2>/dev/null";

    if (system(cmd.c_str()) != 0) {
        std::cerr << "[!!] could not apply cake priority qdisc on " << iface << " (non-critical)\n";
    }
}


void PriorityScheduler::revert_qdisc_hierarchy(const std::string& iface) {
    if (iface.empty()) return;

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    system(("tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null").c_str());
}


int PriorityScheduler::primary_focus_pid(WindowFocusBackend backend) {
    return WindowFocusDetector::get_focused_pid(backend);
}


int PriorityScheduler::secondary_cursor_pid(WindowFocusBackend backend) {
    return WindowFocusDetector::get_pid_under_cursor(backend);
}


void PriorityScheduler::poll_loop(const std::string& iface, std::atomic<bool>& running) {
    (void)iface;

    WindowFocusBackend backend = WindowFocusDetector::detect_available_backend();
    if (backend == WindowFocusBackend::None) return;
    if (!cgroup_v2_mounted()) return;

    FocusDecayTracker focus_tracker;
    ContextPersistence context_persistence;
    NetworkFlowClassifier flow_classifier;
    bool audio_available = AudioActivityMonitor::available();

    std::unordered_map<int, PriorityTier> current_tier;
    std::unordered_map<int, int> ticks_in_tier;

    int tick_count = 0;

    while (running.load(std::memory_order_relaxed)) {
        int keyboard_focus_pid = primary_focus_pid(backend);
        int active_pid = keyboard_focus_pid > 0 ? keyboard_focus_pid : secondary_cursor_pid(backend);

        if (active_pid > 0) {
            focus_tracker.mark_focused(active_pid);
            context_persistence.record_focus_change(active_pid);

            if (context_persistence.has_companion(active_pid)) {
                int companion = context_persistence.companion_pid(active_pid);

                if (companion > 0) {
                    focus_tracker.mark_focused(companion);
                }
            }
        }

        std::unordered_set<int> audio_active_pids;
        if (audio_available && tick_count % kNetworkRescanEveryTicks == 0) {
            audio_active_pids = AudioActivityMonitor::pids_playing_audio();
        }

        if (tick_count % kNetworkRescanEveryTicks == 0) {
            for (int pid : focus_tracker.tracked_pids()) {
                auto activity = ProcessActivity::read_net_activity(pid);
                bool has_activity = activity.has_established_tcp || activity.udp_socket_count > 0;

                focus_tracker.update_network_activity(pid, has_activity);

                if (has_activity) {
                    flow_classifier.sample(pid);
                } else {
                    flow_classifier.forget(pid);
                }
            }
        }

        for (int pid : focus_tracker.tracked_pids()) {
            bool currently_focused = focus_tracker.is_currently_focused(pid);
            bool has_network_activity = focus_tracker.has_network_activity(pid);
            bool rescued_by_flow_shape = has_network_activity && flow_classifier.looks_interactive(pid);

            if (!currently_focused && !has_network_activity) {
                continue;
            }

            double decay_factor = focus_tracker.decay_factor_for(pid);

            if (rescued_by_flow_shape) {
                decay_factor = std::max(decay_factor, 0.75);
            }

            bool demoted_by_audio_noise = !currently_focused && audio_available && audio_active_pids.count(pid) > 0 && !rescued_by_flow_shape;

            if (demoted_by_audio_noise) {
                decay_factor = std::min(decay_factor, 0.2);
            }

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