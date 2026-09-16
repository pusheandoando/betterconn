// core/include/betterconn/priority_scheduler.hpp
#pragma once

#include "betterconn/priority_tier.hpp"
#include "betterconn/focus_decay_tracker.hpp"
#include "betterconn/session_environment.hpp"
#include "betterconn/context_persistence.hpp"
#include "betterconn/window_focus_backend.hpp"
#include "betterconn/audio_activity_monitor.hpp"
#include "betterconn/network_flow_classifier.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>





namespace betterconn {
class PriorityScheduler {
public:
    void start();
    void stop();

    static void apply_cgroup_hierarchy();
    static void revert_cgroup_hierarchy();
    static std::vector<std::string> cgroup_setup_commands();

private:
    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    static constexpr int kPollIntervalMs = 500;
    static constexpr int kNetworkRescanEveryTicks = 6;
    static constexpr int kMinTierDwellTicks = 3;
    static constexpr int kStaleEntryPruneEveryTicks = 40;
    static constexpr int kSessionRetryEveryTicks = 20;
    static constexpr int kMissingFocusTicksBeforeRedetect = 20;

    static void poll_loop(std::atomic<bool>& running);
    static PriorityTier tier_for_decay(double decay_factor);
    static bool tier_boundary_crossed_with_margin(double decay_factor, PriorityTier current_tier, PriorityTier proposed_tier);
    static void move_pid_to_tier(int pid, PriorityTier tier);
    static bool cgroup_v2_mounted();
    static void drain_tier_cgroup(const std::string& tier_path);
};
}