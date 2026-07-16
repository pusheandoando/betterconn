// core/src/context_persistence.cpp
#include "betterconn/context_persistence.hpp"

#include <algorithm>





namespace betterconn {
ContextPersistence::ContextPersistence(size_t history_size) : history_size_(history_size) {
}

void ContextPersistence::record_focus_change(int pid) {
    if (pid <= 0) return;

    if (last_recorded_pid_ > 0 && last_recorded_pid_ != pid) {
        auto& from_map = transitions_[last_recorded_pid_];
        auto& stat = from_map[pid];

        stat.occurrences += 1;

        for (auto& target_pair : from_map) {
            target_pair.second.total_from_occurrences += 1;
        }
    }

    last_recorded_pid_ = pid;

    recent_focus_history_.push_back(pid);
    if (recent_focus_history_.size() > history_size_) {
        recent_focus_history_.pop_front();
    }
}

bool ContextPersistence::alternation_pattern_detected(const std::deque<int>& history, int pid_a, int pid_b) {
    if (history.size() < 4) return false;

    size_t alternations = 0;

    for (size_t i = 1; i < history.size(); ++i) {
        bool switches_between_pair = (history[i - 1] == pid_a && history[i] == pid_b) || (history[i - 1] == pid_b && history[i] == pid_a);

        if (switches_between_pair) ++alternations;
    }

    return alternations >= 3;
}

bool ContextPersistence::has_companion(int pid) const {
    return companion_pid(pid) > 0;
}

int ContextPersistence::companion_pid(int pid) const {
    if (recent_focus_history_.empty()) return -1;

    std::unordered_map<int, int> co_occurrence_counts;

    for (int candidate : recent_focus_history_) {
        if (candidate == pid || candidate <= 0) continue;
        co_occurrence_counts[candidate] += 1;
    }

    int best_candidate = -1;
    int best_count = 0;

    for (const auto& entry : co_occurrence_counts) {
        if (entry.second > best_count && alternation_pattern_detected(recent_focus_history_, pid, entry.first)) {
            best_count = entry.second;
            best_candidate = entry.first;
        }
    }

    return best_candidate;
}

double ContextPersistence::transition_probability(int from_pid, int to_pid) const {
    auto from_it = transitions_.find(from_pid);
    if (from_it == transitions_.end()) return 0.0;

    auto to_it = from_it->second.find(to_pid);
    if (to_it == from_it->second.end()) return 0.0;

    return to_it->second.probability();
}

int ContextPersistence::predicted_next_pid(int current_pid) const {
    auto from_it = transitions_.find(current_pid);
    if (from_it == transitions_.end()) return -1;

    int best_pid = -1;
    double best_probability = 0.0;

    for (const auto& entry : from_it->second) {
        double probability = entry.second.probability();

        if (probability > best_probability) {
            best_probability = probability;
            best_pid = entry.first;
        }
    }

    if (best_probability < 0.3) return -1;

    return best_pid;
}
}