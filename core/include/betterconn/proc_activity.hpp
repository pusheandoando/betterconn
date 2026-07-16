// core/include/betterconn/proc_activity.hpp
#pragma once

#include <string>
#include <unordered_set>





namespace betterconn {
struct ProcessNetActivity {
    uint64_t tcp_socket_count;
    uint64_t udp_socket_count;
    bool has_established_tcp;
};

class ProcessActivity {
public:
    static ProcessNetActivity read_net_activity(int pid);
    static double seconds_since_last_input();

private:
    static std::unordered_set<uint64_t> collect_socket_inodes(int pid);
    static std::unordered_set<uint64_t> parse_proc_net_table(const std::string& path);
    static double read_evdev_last_activity_seconds();
};
}