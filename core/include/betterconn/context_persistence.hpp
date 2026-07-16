// core/include/betterconn/context_persistence.hpp
#pragma once

#include <deque>
#include <string>
#include <utility>
#include <unordered_map>





namespace betterconn {
struct TransitionStat {
    uint64_t occurrences = 0;
    uint64_t total_from_occurrences = 0;

    double probability() const {
        if (total_from_occurrences == 0) return 0.0;
        
        return static_cast<double>(occurrences) / static_cast<double>(total_from_occurrences);
    }
};

class ContextPersistence {
public:
    explicit ContextPersistence(size_t history_size = 32);

    void record_focus_change(int pid);
    bool has_companion(int pid) const;
    int companion_pid(int pid) const;
    double transition_probability(int from_pid, int to_pid) const;
    int predicted_next_pid(int current_pid) const;

private:
    std::deque<int> recent_focus_history_;
    size_t history_size_;
    std::unordered_map<int, std::unordered_map<int, TransitionStat>> transitions_;
    int last_recorded_pid_ = -1;

    static bool alternation_pattern_detected(const std::deque<int>& history, int pid_a, int pid_b);
};
}