// core/src/priority_score.cpp
#include "betterconn/priority_score.hpp"

#include <cmath>
#include <algorithm>





namespace betterconn {
PriorityScoreEngine::PriorityScoreEngine(double heat_decay_per_tick, double recency_tau_seconds)
    : heat_decay_per_tick_(heat_decay_per_tick), recency_tau_seconds_(recency_tau_seconds) {
}

double PriorityScoreEngine::recency_weight_factor(double seconds_since_last_use, double tau) {
    if (tau <= 0.0) return 0.0;

    return std::exp(-seconds_since_last_use / tau);
}

void PriorityScoreEngine::apply_decay() {
    for (auto& entry : states_) {
        entry.second.heat *= heat_decay_per_tick_;
        entry.second.last_seen_seconds_ago += 1.0;
    }
}

double PriorityScoreEngine::compute_score(const ProcessHeatState& state) const {
    double heat_component = std::clamp(state.heat, 0.0, 100.0);
    double network_component = state.has_network_activity ? 100.0 : 0.0;
    double input_component = state.currently_focused ? (100.0 - std::min(last_input_seconds_ago_, 100.0)) : 0.0;
    double recency_component = recency_weight_factor(state.last_seen_seconds_ago, recency_tau_seconds_) * 100.0;
    double class_component = state.classifier.interactivity_weight() * 100.0;

    double score = weights_.heat_weight * heat_component
        + weights_.network_weight * network_component
        + weights_.input_weight * input_component
        + weights_.recency_weight * recency_component
        + weights_.class_weight * class_component;

    return std::clamp(score, 0.0, 100.0);
}

void PriorityScoreEngine::tick(int focused_pid, const std::string& focused_process_name, double seconds_since_input) {
    apply_decay();

    if (seconds_since_input >= 0.0) {
        last_input_seconds_ago_ = seconds_since_input;
    } else {
        last_input_seconds_ago_ = std::min(last_input_seconds_ago_ + 1.0, 100.0);
    }

    for (auto& entry : states_) {
        entry.second.currently_focused = false;
    }

    if (focused_pid > 0) {
        auto& state = states_[focused_pid];
        state.heat = std::min(state.heat + 1.0, 100.0);
        state.last_seen_seconds_ago = 0.0;
        state.currently_focused = true;
        state.process_name = focused_process_name;
    }

    for (auto& entry : states_) {
        entry.second.last_score = compute_score(entry.second);
    }
}

void PriorityScoreEngine::report_network_activity(int pid, const NetworkActivitySample& sample) {
    auto it = states_.find(pid);
    if (it == states_.end()) return;

    it->second.classifier.observe(sample);
    it->second.has_network_activity = sample.has_established_tcp || sample.udp_socket_count > 0;
}

std::unordered_map<int, ProcessHeatState> PriorityScoreEngine::snapshot() const {
    return states_;
}

double PriorityScoreEngine::score_for(int pid) const {
    auto it = states_.find(pid);
    if (it == states_.end()) return 0.0;

    return it->second.last_score;
}
}