// core/include/betterconn/focus_decay_tracker.hpp
#pragma once

#include <chrono>
#include <vector>
#include <unordered_map>





namespace betterconn {
struct FocusDecayState {
    std::chrono::steady_clock::time_point last_focus_time;
    bool has_network_activity = false;
};

class FocusDecayTracker {
public:
    explicit FocusDecayTracker(double half_life_seconds = 25.0);

    void mark_focused(int pid);
    void update_network_activity(int pid, bool has_activity);
    void forget_stale_entries(double max_age_seconds = 300.0);

    bool is_currently_focused(int pid) const;
    double decay_factor_for(int pid) const;
    bool has_network_activity(int pid) const;
    std::vector<int> tracked_pids() const;

private:
    double half_life_seconds_;
    int currently_focused_pid_ = -1;
    std::unordered_map<int, FocusDecayState> states_;
};
}