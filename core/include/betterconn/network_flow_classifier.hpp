// core/include/betterconn/network_flow_classifier.hpp
#pragma once

#include "betterconn/proc_activity.hpp"

#include <deque>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>





namespace betterconn {
class NetworkFlowClassifier {
public:
    void sample(int pid, const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot);
    bool looks_interactive(int pid) const;
    void forget(int pid);

private:
    static constexpr size_t kSampleWindow = 6;
    static constexpr uint64_t kBulkQueueThresholdBytes = 65536;

    struct PidSamples {
        std::deque<uint64_t> queue_occupancy_bytes;
    };

    std::unordered_map<int, PidSamples> samples_;

    static uint64_t queue_occupancy(const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot);
    static double sample_variance(const std::deque<uint64_t>& values);
};
}