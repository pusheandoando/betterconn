// core/include/betterconn/priority_score.hpp
#pragma once

#include "betterconn/app_classifier.hpp"

#include <string>
#include <unordered_map>





namespace betterconn {
struct ProcessHeatState {
    double heat = 0.0;
    double last_seen_seconds_ago = 0.0;
    double last_score = 0.0;
    std::string process_name;
    AppClassifier classifier;
    bool has_network_activity = false;
    bool currently_focused = false;
};

struct PriorityWeights {
    double heat_weight = 0.40;
    double network_weight = 0.25;
    double input_weight = 0.15;
    double recency_weight = 0.10;
    double class_weight = 0.10;
};

class PriorityScoreEngine {
public:
    explicit PriorityScoreEngine(double heat_decay_per_tick = 0.985, double recency_tau_seconds = 90.0);

    void tick(int focused_pid, const std::string& focused_process_name, double seconds_since_input);
    void report_network_activity(int pid, const NetworkActivitySample& sample);

    std::unordered_map<int, ProcessHeatState> snapshot() const;
    double score_for(int pid) const;

private:
    std::unordered_map<int, ProcessHeatState> states_;
    PriorityWeights weights_;
    double heat_decay_per_tick_;
    double recency_tau_seconds_;
    double last_input_seconds_ago_ = 9999.0;

    static double recency_weight_factor(double seconds_since_last_use, double tau);
    void apply_decay();
    double compute_score(const ProcessHeatState& state) const;
};
}