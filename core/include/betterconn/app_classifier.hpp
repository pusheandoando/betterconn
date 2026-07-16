// core/include/betterconn/app_classifier.hpp
#pragma once

#include <deque>
#include <string>
#include <cstdint>





namespace betterconn {
struct NetworkActivitySample {
    uint64_t tcp_socket_count = 0;
    uint64_t udp_socket_count = 0;
    bool has_established_tcp = false;
};

class AppClassifier {
public:
    explicit AppClassifier(size_t history_size = 12);

    void observe(const NetworkActivitySample& sample);
    double interactivity_weight() const;
    bool has_sustained_network_activity() const;

private:
    struct SocketCountSample {
        uint64_t tcp_socket_count;
        uint64_t udp_socket_count;
    };

    std::deque<SocketCountSample> history_;
    size_t history_size_;
    bool last_had_established_tcp_ = false;
    bool last_had_udp_ = false;

    double socket_count_stability() const;
    double established_activity_ratio() const;
};
}