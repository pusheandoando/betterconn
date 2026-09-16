// core/src/network_flow_classifier.cpp
#include "betterconn/network_flow_classifier.hpp"

#include <algorithm>





namespace betterconn {
uint64_t NetworkFlowClassifier::queue_occupancy(const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot) {
    uint64_t total = 0;

    for (uint64_t inode : socket_inodes) {
        auto it = snapshot.queued_bytes_by_inode.find(inode);

        if (it == snapshot.queued_bytes_by_inode.end()) continue;

        total += it->second;
    }

    return total;
}


double NetworkFlowClassifier::sample_variance(const std::deque<uint64_t>& values) {
    if (values.size() < 2) return 0.0;

    double mean = 0.0;

    for (uint64_t v : values) mean += static_cast<double>(v);
    
    mean /= static_cast<double>(values.size());

    double variance = 0.0;
    for (uint64_t v : values) {
        double delta = static_cast<double>(v) - mean;
        variance += delta * delta;
    }

    return variance / static_cast<double>(values.size());
}


void NetworkFlowClassifier::sample(int pid, const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot) {
    if (pid <= 0) return;

    auto& entry = samples_[pid];

    entry.queue_occupancy_bytes.push_back(queue_occupancy(socket_inodes, snapshot));

    if (entry.queue_occupancy_bytes.size() > kSampleWindow) {
        entry.queue_occupancy_bytes.pop_front();
    }
}


bool NetworkFlowClassifier::looks_interactive(int pid) const {
    auto it = samples_.find(pid);
    if (it == samples_.end() || it->second.queue_occupancy_bytes.size() < kSampleWindow) return false;

    const auto& values = it->second.queue_occupancy_bytes;

    uint64_t max_occupancy = 0;
    for (uint64_t v : values) max_occupancy = std::max(max_occupancy, v);

    if (max_occupancy >= kBulkQueueThresholdBytes) return false;

    double variance = sample_variance(values);

    return variance > 0.0;
}


void NetworkFlowClassifier::forget(int pid) {
    samples_.erase(pid);
}
}