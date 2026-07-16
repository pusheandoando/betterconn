// core/src/app_classifier.cpp
#include "betterconn/app_classifier.hpp"

#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>





namespace betterconn {
AppClassifier::AppClassifier(size_t history_size) : history_size_(history_size) {
}

void AppClassifier::observe(const NetworkActivitySample& sample) {
    history_.push_back({sample.tcp_socket_count, sample.udp_socket_count});

    if (history_.size() > history_size_) {
        history_.pop_front();
    }

    last_had_established_tcp_ = sample.has_established_tcp;
    last_had_udp_ = sample.udp_socket_count > 0;
}

double AppClassifier::socket_count_stability() const {
    if (history_.size() < 3) return 0.0;

    std::vector<double> totals;
    totals.reserve(history_.size());

    for (const auto& entry : history_) {
        totals.push_back(static_cast<double>(entry.tcp_socket_count + entry.udp_socket_count));
    }

    double mean = std::accumulate(totals.begin(), totals.end(), 0.0) / totals.size();
    if (mean <= 0.0) return 0.0;

    double variance = 0.0;
    for (double value : totals) {
        double diff = value - mean;
        variance += diff * diff;
    }
    variance /= totals.size();

    double coefficient_of_variation = std::sqrt(variance) / mean;

    return std::clamp(1.0 - coefficient_of_variation, 0.0, 1.0);
}

double AppClassifier::established_activity_ratio() const {
    if (history_.empty()) return 0.0;

    size_t active_ticks = 0;

    for (const auto& entry : history_) {
        if (entry.tcp_socket_count > 0 || entry.udp_socket_count > 0) ++active_ticks;
    }

    return static_cast<double>(active_ticks) / static_cast<double>(history_.size());
}

double AppClassifier::interactivity_weight() const {
    if (history_.empty()) return 0.5;

    double stability = socket_count_stability();
    double activity_ratio = established_activity_ratio();

    double realtime_bonus = (last_had_udp_ || last_had_established_tcp_) ? stability : 0.0;

    double weight = 0.35 + 0.35 * activity_ratio + 0.30 * realtime_bonus;

    return std::clamp(weight, 0.0, 1.0);
}

bool AppClassifier::has_sustained_network_activity() const {
    return established_activity_ratio() >= 0.6;
}
}