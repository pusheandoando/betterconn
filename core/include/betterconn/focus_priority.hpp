// core/include/betterconn/focus_priority.hpp
#pragma once

#include <atomic>
#include <string>
#include <thread>





namespace betterconn {
class FocusPriority {
public:
    void start(const std::string& iface);
    void stop();

    static void apply_qdisc_hierarchy(const std::string& iface);
    static void revert_qdisc_hierarchy(const std::string& iface);
    static void apply_cgroup_and_marking();
    static void revert_cgroup_and_marking();
    static void set_qdisc_targets(const std::string& iface, int focus_target_ms, int default_target_ms);

private:
    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    static constexpr const char* kCgroupPath = "/sys/fs/cgroup/betterconn_focus";
    static constexpr int kFwMark = 0x1f;
    static constexpr const char* kFocusClassId = "1:10";
    static constexpr const char* kDefaultClassId = "1:20";

    static void poll_loop(const std::string& iface, std::atomic<bool>& running);
    static void move_pid_to_focus_cgroup(int pid);
    static bool cgroup_v2_mounted();
};
}