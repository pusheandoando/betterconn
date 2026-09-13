// core/include/betterconn/priority_scheduler.hpp
#pragma once

#include "betterconn/focus_decay_tracker.hpp"
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
enum class PriorityTier {
    Cold,
    Cool,
    Warm,
    Hot,
};


class PriorityScheduler {
public:
    void start(const std::string& iface);
    void stop();

    static void apply_qdisc_hierarchy(const std::string& iface);
    static void revert_qdisc_hierarchy(const std::string& iface);
    static void apply_cgroup_and_marking();
    static void revert_cgroup_and_marking();

private:
    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    static constexpr int kPollIntervalMs = 500;
    static constexpr int kNetworkRescanEveryTicks = 6;
    static constexpr int kMinTierDwellTicks = 3;
    static constexpr int kStaleEntryPruneEveryTicks = 40;

    static constexpr const char* kCgroupBasePath = "/sys/fs/cgroup/betterconn_priority";
    static constexpr int kHotFwMark = 4;
    static constexpr int kWarmFwMark = 3;
    static constexpr int kCoolFwMark = 2;
    static constexpr int kColdFwMark = 1;

    static void poll_loop(const std::string& iface, std::atomic<bool>& running);
    static PriorityTier tier_for_decay(double decay_factor);
    static bool tier_boundary_crossed_with_margin(double decay_factor, PriorityTier current_tier, PriorityTier proposed_tier);
    static const char* tier_cgroup_name(PriorityTier tier);
    static int tier_fw_mark(PriorityTier tier);
    static void move_pid_to_tier(int pid, PriorityTier tier);
    static bool cgroup_v2_mounted();
    static int primary_focus_pid(WindowFocusBackend backend);
    static int secondary_cursor_pid(WindowFocusBackend backend);
};
}