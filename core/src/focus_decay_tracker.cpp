// core/src/focus_decay_tracker.cpp
#include "betterconn/focus_decay_tracker.hpp"

#include <cmath>





namespace betterconn {
FocusDecayTracker::FocusDecayTracker(double half_life_seconds) : half_life_seconds_(half_life_seconds) {
}

void FocusDecayTracker::mark_focused(int pid) {
    if (pid <= 0) return;

    currently_focused_pid_ = pid;
    states_[pid].last_focus_time = std::chrono::steady_clock::now();
}

void FocusDecayTracker::update_network_activity(int pid, bool has_activity) {
    auto it = states_.find(pid);
    if (it == states_.end()) return;

    it->second.has_network_activity = has_activity;
}

bool FocusDecayTracker::is_currently_focused(int pid) const {
    return pid > 0 && pid == currently_focused_pid_;
}

double FocusDecayTracker::decay_factor_for(int pid) const {
    if (is_currently_focused(pid)) return 1.0;

    auto it = states_.find(pid);
    if (it == states_.end()) return 0.0;

    /*
    Classic decay-usage priority aging (Epema, "Decay-Usage Scheduling in Multiprocessors", ACM TOCS 1998)
    applied to elapsed time since a process last held cursor focus instead of elapsed CPU time, so a
    process that was just defocused starts near full priority and only gradually loses it,
    rather than being demoted instantly.
    */
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - it->second.last_focus_time;
    double elapsed_seconds = elapsed.count();

    return std::exp(-elapsed_seconds / half_life_seconds_);
}

bool FocusDecayTracker::has_network_activity(int pid) const {
    auto it = states_.find(pid);
    if (it == states_.end()) return false;

    return it->second.has_network_activity;
}

std::vector<int> FocusDecayTracker::tracked_pids() const {
    std::vector<int> pids;
    pids.reserve(states_.size());

    for (const auto& entry : states_) {
        pids.push_back(entry.first);
    }

    return pids;
}

void FocusDecayTracker::forget_stale_entries(double max_age_seconds) {
    auto now = std::chrono::steady_clock::now();

    for (auto it = states_.begin(); it != states_.end();) {
        if (it->first == currently_focused_pid_) {
            ++it;
            
            continue;
        }

        std::chrono::duration<double> elapsed = now - it->second.last_focus_time;

        if (elapsed.count() >= max_age_seconds && !it->second.has_network_activity) {
            it = states_.erase(it);
        } else {
            ++it;
        }
    }
}
}