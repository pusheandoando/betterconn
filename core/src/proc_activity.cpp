// core/src/proc_activity.cpp
#include "betterconn/proc_activity.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <filesystem>
#include <sys/stat.h>





namespace betterconn {
std::unordered_set<uint64_t> ProcessActivity::parse_proc_net_table(const std::string& path) {
    std::unordered_set<uint64_t> inodes;
    std::ifstream f(path);
    if (!f) return inodes;

    std::string line;
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string sl, local_addr, rem_addr, st, tx_rx, tr_tm, retr, uid, timeout, inode_str;
        ss >> sl >> local_addr >> rem_addr >> st >> tx_rx >> tr_tm >> retr >> uid >> timeout >> inode_str;

        if (inode_str.empty()) continue;

        try {
            inodes.insert(std::stoull(inode_str));
        } catch (...) {
        }
    }

    return inodes;
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

ProcessNetActivity ProcessActivity::read_net_activity(int pid) {
    ProcessNetActivity activity{0, 0, false};
    if (pid <= 0) return activity;

    auto proc_sockets = collect_socket_inodes(pid);
    if (proc_sockets.empty()) return activity;

    auto tcp_inodes = parse_proc_net_table("/proc/net/tcp");
    auto tcp6_inodes = parse_proc_net_table("/proc/net/tcp6");
    auto udp_inodes = parse_proc_net_table("/proc/net/udp");
    auto udp6_inodes = parse_proc_net_table("/proc/net/udp6");

    for (uint64_t inode : proc_sockets) {
        bool is_tcp = tcp_inodes.count(inode) > 0 || tcp6_inodes.count(inode) > 0;
        bool is_udp = udp_inodes.count(inode) > 0 || udp6_inodes.count(inode) > 0;

        if (is_tcp) {
            activity.tcp_socket_count += 1;
            activity.has_established_tcp = true;
        }

        if (is_udp) {
            activity.udp_socket_count += 1;
        }
    }

    return activity;
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