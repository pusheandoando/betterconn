// core/src/proc_activity.cpp
#include "betterconn/proc_activity.hpp"

#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <filesystem>
#include <sys/stat.h>





namespace betterconn {
static uint64_t parse_queue_bytes(const std::string& field) {
    auto colon = field.find(':');
    if (colon == std::string::npos) return 0;

    try {
        return std::stoull(field.substr(0, colon), nullptr, 16) + std::stoull(field.substr(colon + 1), nullptr, 16);
    } catch (...) {
        return 0;
    }
}


void ProcessActivity::merge_proc_net_table(const std::string& path, bool tcp_table, SocketStateSnapshot& snapshot) {
    std::ifstream f(path);

    if (!f) return;

    std::string line;
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string sl, local_addr, rem_addr, state, queues, timers, retransmits, uid, timeout, inode_str;

        ss >> sl >> local_addr >> rem_addr >> state >> queues >> timers >> retransmits >> uid >> timeout >> inode_str;

        if (inode_str.empty()) continue;

        uint64_t inode = 0;

        try {
            inode = std::stoull(inode_str);
        } catch (...) {
            continue;
        }

        if (inode == 0) continue;

        if (tcp_table) {
            snapshot.tcp_inodes.insert(inode);

            // State 01 is TCP_ESTABLISHED, every other value is a listener or a socket already tearing down
            if (state == "01") snapshot.established_tcp_inodes.insert(inode);
        } else {
            snapshot.udp_inodes.insert(inode);
        }

        snapshot.queued_bytes_by_inode[inode] = parse_queue_bytes(queues);
    }
}





SocketStateSnapshot ProcessActivity::capture_socket_state() {
    SocketStateSnapshot snapshot;

    merge_proc_net_table("/proc/net/tcp", true, snapshot);
    merge_proc_net_table("/proc/net/tcp6", true, snapshot);
    merge_proc_net_table("/proc/net/udp", false, snapshot);
    merge_proc_net_table("/proc/net/udp6", false, snapshot);

    return snapshot;
}





std::unordered_set<uint64_t> ProcessActivity::collect_socket_inodes(int pid) {
    std::unordered_set<uint64_t> inodes;
    std::string fd_dir = "/proc/" + std::to_string(pid) + "/fd";

    DIR* dir = opendir(fd_dir.c_str());
    if (!dir) return inodes;

    char link_target[256];
    struct dirent* entry;

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        std::string fd_path = fd_dir + "/" + entry->d_name;

        ssize_t len = readlink(fd_path.c_str(), link_target, sizeof(link_target) - 1);
        if (len <= 0) continue;

        link_target[len] = '\0';
        std::string target(link_target);

        auto open_paren = target.find("socket:[");
        if (open_paren == std::string::npos) continue;

        auto close_paren = target.find(']', open_paren);
        if (close_paren == std::string::npos) continue;

        std::string inode_str = target.substr(open_paren + 8, close_paren - open_paren - 8);

        try {
            inodes.insert(std::stoull(inode_str));
        } catch (...) {
        }
    }

    closedir(dir);

    return inodes;
}





ProcessNetActivity ProcessActivity::summarize(const std::unordered_set<uint64_t>& socket_inodes, const SocketStateSnapshot& snapshot) {
    ProcessNetActivity activity{0, 0, false};

    for (uint64_t inode : socket_inodes) {
        if (snapshot.tcp_inodes.count(inode) > 0) {
            activity.tcp_socket_count += 1;

            if (snapshot.established_tcp_inodes.count(inode) > 0) activity.has_established_tcp = true;
        }

        if (snapshot.udp_inodes.count(inode) > 0) {
            activity.udp_socket_count += 1;
        }
    }

    return activity;
}





std::unordered_set<uint64_t> ProcessActivity::collect_socket_inodes_for(int pid) {
    if (pid <= 0) return {};

    return collect_socket_inodes(pid);
}


double ProcessActivity::read_evdev_last_activity_seconds() {
    std::string input_dir = "/dev/input";

    if (!std::filesystem::exists(input_dir)) return -1.0;

    double newest_delta = -1.0;
    struct stat st;


    for (const auto& entry : std::filesystem::directory_iterator(input_dir)) {
        std::string name = entry.path().filename().string();
        if (name.find("event") != 0) continue;

        if (stat(entry.path().c_str(), &st) != 0) continue;

        time_t now = time(nullptr);
        double delta = static_cast<double>(now - st.st_mtime);

        if (newest_delta < 0.0 || delta < newest_delta) {
            newest_delta = delta;
        }
    }

    return newest_delta;
}


double ProcessActivity::seconds_since_last_input() {
    double delta = read_evdev_last_activity_seconds();
    
    return delta;
}
}