// core/include/betterconn/proc_activity.hpp
#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>





namespace betterconn {
struct ProcessNetActivity {
    uint64_t tcp_socket_count;
    uint64_t udp_socket_count;
    bool has_established_tcp;
};


struct SocketStateSnapshot {
    std::unordered_set<uint64_t> tcp_inodes;
    std::unordered_set<uint64_t> established_tcp_inodes;
    std::unordered_set<uint64_t> udp_inodes;
    std::unordered_map<uint64_t, uint64_t> queued_bytes_by_inode;
};


class ProcessActivity {
public:
    static SocketStateSnapshot capture_socket_state();
    static ProcessNetActivity summarize(const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot);
    static double seconds_since_last_input();
    static std::unordered_set<uint64_t> collect_socket_inodes_for(int pid);

private:
    static std::unordered_set<uint64_t> collect_socket_inodes(int pid);
    static void merge_proc_net_table(const std::string& path, bool tcp_table, SocketStateSnapshot& snapshot);
    static double read_evdev_last_activity_seconds();
};
}