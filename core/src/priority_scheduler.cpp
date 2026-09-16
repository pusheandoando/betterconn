// core/src/priority_scheduler.cpp
#include "betterconn/proc_activity.hpp"
#include "betterconn/priority_scheduler.hpp"

#include <array>
#include <chrono>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>





namespace betterconn {
namespace {
const std::array<PriorityTier, 4> kAllTiers = {
    PriorityTier::Hot, PriorityTier::Warm, PriorityTier::Cool, PriorityTier::Cold
};


std::string tier_path_for(PriorityTier tier) {
    return std::string(kPriorityCgroupBase) + "/" + tier_cgroup_name(tier);
}
}


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


std::vector<std::string> PriorityScheduler::cgroup_setup_commands() {
    std::vector<std::string> commands;

    for (PriorityTier tier : kAllTiers) {
        commands.push_back("mkdir -p " + tier_path_for(tier) + " 2>/dev/null");
    }

    return commands;
}


void PriorityScheduler::apply_cgroup_hierarchy() {
    if (!cgroup_v2_mounted()) {
        std::cerr << "[!!] cgroup v2 not mounted, priority scheduling disabled\n";
        return;
    }

    for (PriorityTier tier : kAllTiers) {
        std::error_code ec;
        std::string tier_path = tier_path_for(tier);

        std::filesystem::create_directories(tier_path, ec);

        if (ec) {
            std::cerr << "[!!] could not create priority cgroup " << tier_path << " (non-critical)\n";
        }
    }
}


void PriorityScheduler::drain_tier_cgroup(const std::string& tier_path) {
    std::vector<int> pids;

    {
        std::ifstream f(tier_path + "/cgroup.procs");
        if (!f) return;

        std::string line;

        while (std::getline(f, line)) {
            try {
                pids.push_back(std::stoi(line));
            } catch (...) {
            }
        }
    }

    // The kernel only removes an empty cgroup, and its control files cannot be unlinked, so every process has to be handed back to the root group before the directory goes away
    for (int pid : pids) {
        std::ofstream root("/sys/fs/cgroup/cgroup.procs");

        if (!root) return;

        root << pid;
    }
}


void PriorityScheduler::revert_cgroup_hierarchy() {
    for (PriorityTier tier : kAllTiers) {
        std::string tier_path = tier_path_for(tier);

        if (!std::filesystem::exists(tier_path)) continue;

        drain_tier_cgroup(tier_path);
        rmdir(tier_path.c_str());
    }

    rmdir(kPriorityCgroupBase);
}


void PriorityScheduler::move_pid_to_tier(int pid, PriorityTier tier) {
    if (pid <= 0) return;

    std::string tier_path = tier_path_for(tier);
    if (!std::filesystem::exists(tier_path)) return;

    std::ofstream f(tier_path + "/cgroup.procs");
    if (!f) return;

    f << pid;
}


void PriorityScheduler::poll_loop(std::atomic<bool>& running) {
    if (!cgroup_v2_mounted()) return;

    GraphicalSession session = SessionEnvironment::detect();
    std::string env_prefix = SessionEnvironment::command_prefix(session);
    WindowFocusBackend backend = WindowFocusDetector::detect_available_backend(env_prefix);

    FocusDecayTracker focus_tracker;
    ContextPersistence context_persistence;
    NetworkFlowClassifier flow_classifier;
    bool audio_available = AudioActivityMonitor::available();

    std::unordered_map<int, PriorityTier> current_tier;
    std::unordered_map<int, int> ticks_in_tier;
    std::unordered_set<int> audio_active_pids;

    int tick_count = 0;
    int ticks_without_focus = 0;

    while (running.load(std::memory_order_relaxed)) {
        // The daemon is started at boot, long before anyone logs in, so the session has to be picked up again whenever the focus backend is missing or stops answering
        bool session_unusable = (backend == WindowFocusBackend::None) || (ticks_without_focus >= kMissingFocusTicksBeforeRedetect);

        if (session_unusable && tick_count % kSessionRetryEveryTicks == 0) {
            session = SessionEnvironment::detect();
            env_prefix = SessionEnvironment::command_prefix(session);
            backend = WindowFocusDetector::detect_available_backend(env_prefix);
            ticks_without_focus = 0;
        }

        int active_pid = -1;

        if (backend != WindowFocusBackend::None) {
            int keyboard_focus_pid = WindowFocusDetector::get_focused_pid(backend, env_prefix);

            active_pid = keyboard_focus_pid > 0 ? keyboard_focus_pid : WindowFocusDetector::get_pid_under_cursor(backend, env_prefix);
        }

        if (active_pid > 0) {
            ticks_without_focus = 0;

            focus_tracker.mark_focused(active_pid);
            context_persistence.record_focus_change(active_pid);

            if (context_persistence.has_companion(active_pid)) {
                int companion = context_persistence.companion_pid(active_pid);

                if (companion > 0) {
                    focus_tracker.mark_recently_used(companion);
                }
            }
        } else {
            ++ticks_without_focus;
        }

        if (tick_count % kNetworkRescanEveryTicks == 0) {
            if (audio_available) {
                audio_active_pids = AudioActivityMonitor::pids_playing_audio();
            }

            SocketStateSnapshot snapshot = ProcessActivity::capture_socket_state();

            for (int pid : focus_tracker.tracked_pids()) {
                auto socket_inodes = ProcessActivity::collect_socket_inodes_for(pid);
                ProcessNetActivity activity = ProcessActivity::summarize(socket_inodes, snapshot);
                bool has_activity = activity.has_established_tcp || activity.udp_socket_count > 0;

                focus_tracker.update_network_activity(pid, has_activity);

                if (has_activity) {
                    flow_classifier.sample(pid, socket_inodes, snapshot);
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

            auto tracked = focus_tracker.tracked_pids();
            std::unordered_set<int> live_pids(tracked.begin(), tracked.end());

            for (auto it = current_tier.begin(); it != current_tier.end();) {
                if (live_pids.count(it->first) == 0) {
                    ticks_in_tier.erase(it->first);
                    flow_classifier.forget(it->first);
                    it = current_tier.erase(it);
                } else {
                    ++it;
                }
            }
        }

        ++tick_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}


void PriorityScheduler::start() {
    running_.store(true, std::memory_order_relaxed);

    poll_thread_ = std::thread([this]() {
        poll_loop(running_);
    });
}


void PriorityScheduler::stop() {
    running_.store(false, std::memory_order_relaxed);

    if (poll_thread_.joinable()) poll_thread_.join();
}
}