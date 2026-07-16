// core/src/focus_priority.cpp
#include "betterconn/focus_priority.hpp"
#include "betterconn/window_focus_backend.hpp"

#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <filesystem>





namespace betterconn {
static constexpr int kPollIntervalMs = 500;

bool FocusPriority::cgroup_v2_mounted() {
    return std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers");
}

void FocusPriority::apply_cgroup_and_marking() {
    if (!cgroup_v2_mounted()) {
        std::cerr << "[!!] cgroup v2 not mounted, focus-based prioritization disabled\n";
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(kCgroupPath, ec);

    if (ec) {
        std::cerr << "[!!] could not create focus cgroup (non-critical)\n";
        return;
    }

    std::string mark_hex = "0x" + [] {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", kFwMark);
        
        return std::string(buf);
    }();

    std::string check_cmd = "iptables -t mangle -C OUTPUT -m cgroup --path betterconn_focus -j MARK --set-mark " + mark_hex + " 2>/dev/null";
    std::string add_cmd = "iptables -t mangle -A OUTPUT -m cgroup --path betterconn_focus -j MARK --set-mark " + mark_hex + " 2>/dev/null";

    if (system(check_cmd.c_str()) != 0) {
        if (system(add_cmd.c_str()) != 0) {
            std::cerr << "[!!] could not add focus marking rule (non-critical)\n";
        }
    }
}

void FocusPriority::revert_cgroup_and_marking() {
    std::string mark_hex = "0x" + [] {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", kFwMark);
        
        return std::string(buf);
    }();

    std::string del_cmd = "iptables -t mangle -D OUTPUT -m cgroup --path betterconn_focus -j MARK --set-mark " + mark_hex + " 2>/dev/null";
    system(del_cmd.c_str());

    std::error_code ec;
    std::filesystem::remove(kCgroupPath, ec);
}

void FocusPriority::move_pid_to_focus_cgroup(int pid) {
    if (pid <= 0) return;
    if (!std::filesystem::exists(kCgroupPath)) return;

    std::ofstream f(std::string(kCgroupPath) + "/cgroup.procs");
    if (!f) return;

    f << pid;
}

void FocusPriority::apply_qdisc_hierarchy(const std::string& iface) {
    if (iface.empty()) return;

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());

    bool ok = true;
    ok &= system(("tc qdisc replace dev " + iface + " root handle 1: htb default 20 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1: classid 1:1 htb rate 1000mbit ceil 1000mbit 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kFocusClassId) + " htb rate 300mbit ceil 1000mbit prio 1 2>/dev/null").c_str()) == 0;
    ok &= system(("tc class add dev " + iface + " parent 1:1 classid " + std::string(kDefaultClassId) + " htb rate 700mbit ceil 1000mbit prio 2 2>/dev/null").c_str()) == 0;
    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kFocusClassId) + " handle 10: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;
    ok &= system(("tc qdisc add dev " + iface + " parent " + std::string(kDefaultClassId) + " handle 20: fq_codel target 5ms interval 100ms 2>/dev/null").c_str()) == 0;

    std::string mark_hex = "0x" + [] {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", kFwMark);
        
        return std::string(buf);
    }();

    ok &= system(("tc filter add dev " + iface + " parent 1: protocol ip prio 1 handle " + mark_hex + " fw flowid " + std::string(kFocusClassId) + " 2>/dev/null").c_str()) == 0;

    if (!ok) {
        std::cerr << "[!!] could not fully apply focus-priority qdisc hierarchy on " << iface << " (non-critical)\n";
        
        return;
    }
}

void FocusPriority::revert_qdisc_hierarchy(const std::string& iface) {
    if (iface.empty()) return;

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    system(("tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null").c_str());
}

void FocusPriority::set_qdisc_targets(const std::string& iface, int focus_target_ms, int default_target_ms) {
    std::string focus_cmd = "tc qdisc replace dev " + iface + " parent " + std::string(kFocusClassId) +
        " handle 10: fq_codel target " + std::to_string(focus_target_ms) + "ms interval " +
        std::to_string(focus_target_ms * 20) + "ms 2>/dev/null";

    std::string default_cmd = "tc qdisc replace dev " + iface + " parent " + std::string(kDefaultClassId) +
        " handle 20: fq_codel target " + std::to_string(default_target_ms) + "ms interval " +
        std::to_string(default_target_ms * 20) + "ms 2>/dev/null";

    system(focus_cmd.c_str());
    system(default_cmd.c_str());
}

void FocusPriority::poll_loop(const std::string& iface, std::atomic<bool>& running) {
    (void)iface;

    WindowFocusBackend backend = WindowFocusDetector::detect_available_backend();
    if (backend == WindowFocusBackend::None) return;
    if (!cgroup_v2_mounted()) return;

    int last_pid = -1;

    while (running.load(std::memory_order_relaxed)) {
        int pid = WindowFocusDetector::get_focused_pid(backend);

        if (pid > 0 && pid != last_pid) {
            move_pid_to_focus_cgroup(pid);
            last_pid = pid;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}

void FocusPriority::start(const std::string& iface) {
    running_.store(true, std::memory_order_relaxed);

    poll_thread_ = std::thread([iface, this]() {
        poll_loop(iface, running_);
    });
}

void FocusPriority::stop() {
    running_.store(false, std::memory_order_relaxed);
    
    if (poll_thread_.joinable()) poll_thread_.join();
}
}