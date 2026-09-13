// core/src/network_flow_classifier.cpp
#include "betterconn/network_flow_classifier.hpp"
#include "betterconn/proc_activity.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_set>





namespace betterconn {
static uint64_t hex_queue_bytes(const std::string& field) {
    auto colon = field.find(':');
    if (colon == std::string::npos) return 0;

    try {
        return std::stoull(field.substr(0, colon), nullptr, 16) + std::stoull(field.substr(colon + 1), nullptr, 16);
    } catch (...) {
        return 0;
    }
}


static uint64_t sum_queue_bytes_for_inodes(const std::string& path, const std::unordered_set<uint64_t>& inodes) {
    std::ifstream f(path);
    if (!f) return 0;

    std::string line;
    std::getline(f, line);

    uint64_t total = 0;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string sl, local_addr, rem_addr, st, tx_rx, tr_tm, retr, uid, timeout, inode_str;

        ss >> sl >> local_addr >> rem_addr >> st >> tx_rx >> tr_tm >> retr >> uid >> timeout >> inode_str;

        if (inode_str.empty()) continue;

        uint64_t inode = 0;
        try {
            inode = std::stoull(inode_str);
        } catch (...) {
            continue;
        }

        if (inodes.count(inode) == 0) continue;

        total += hex_queue_bytes(tx_rx);
    }

    return total;
}


uint64_t NetworkFlowClassifier::read_socket_queue_occupancy(int pid) {
    auto inodes = ProcessActivity::collect_socket_inodes_for(pid);
    if (inodes.empty()) return 0;

    uint64_t total = 0;
    total += sum_queue_bytes_for_inodes("/proc/net/tcp", inodes);
    total += sum_queue_bytes_for_inodes("/proc/net/tcp6", inodes);
    total += sum_queue_bytes_for_inodes("/proc/net/udp", inodes);
    total += sum_queue_bytes_for_inodes("/proc/net/udp6", inodes);

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


void NetworkFlowClassifier::sample(int pid) {
    if (pid <= 0) return;

    uint64_t occupancy = read_socket_queue_occupancy(pid);
    auto& entry = samples_[pid];

    entry.queue_occupancy_bytes.push_back(occupancy);

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