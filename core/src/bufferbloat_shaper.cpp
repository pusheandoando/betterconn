// core/src/bufferbloat_shaper.cpp
#include "betterconn/bufferbloat_shaper.hpp"

#include <chrono>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>





namespace betterconn {
static uint64_t read_iface_rx_bytes(const std::string& iface) {
    std::ifstream f("/proc/net/dev");
    std::string line;

    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);

        if (name != iface) continue;

        std::istringstream ss(line.substr(colon + 1));
        uint64_t rx;
        ss >> rx;

        return rx;
    }

    return 0ULL;
}

static uint64_t read_iface_tx_bytes(const std::string& iface) {
    std::ifstream f("/proc/net/dev");
    std::string line;

    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);

        if (name != iface) continue;

        std::istringstream ss(line.substr(colon + 1));
        uint64_t rx, dummy, tx;
        ss >> rx;

        for (int i = 0; i < 7; ++i) ss >> dummy;
        ss >> tx;

        return tx;
    }

    return 0ULL;
}

MeasuredThroughput BufferbloatShaper::measure_throughput(const std::string& iface, int sample_ms) {
    if (iface.empty()) return {0.0, 0.0, false};

    uint64_t rx0 = read_iface_rx_bytes(iface);
    uint64_t tx0 = read_iface_tx_bytes(iface);

    std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));

    uint64_t rx1 = read_iface_rx_bytes(iface);
    uint64_t tx1 = read_iface_tx_bytes(iface);

    double seconds = sample_ms / 1000.0;
    double download_bps = static_cast<double>(rx1 - rx0) * 8.0 / seconds;
    double upload_bps = static_cast<double>(tx1 - tx0) * 8.0 / seconds;

    return {download_bps, upload_bps, true};
}

uint64_t BufferbloatShaper::shaped_rate_kbit(double measured_bps) {
    double shaped_bps = measured_bps * kShapingFraction;
    uint64_t rate_kbit = static_cast<uint64_t>(shaped_bps / 1000.0);

    return std::max(rate_kbit, kMinShapedRateKbit);
}

std::string BufferbloatShaper::ifb_name_for(const std::string& iface) {
    return "ifb4" + iface;
}

void BufferbloatShaper::ensure_ifb_module_loaded() {
    system("modprobe ifb numifbs=0 2>/dev/null");
}

void BufferbloatShaper::create_ifb_device(const std::string& ifb_iface) {
    system(("ip link del " + ifb_iface + " 2>/dev/null").c_str());
    system(("ip link add " + ifb_iface + " type ifb 2>/dev/null").c_str());
    system(("ip link set dev " + ifb_iface + " up 2>/dev/null").c_str());
}

void BufferbloatShaper::redirect_ingress_to_ifb(const std::string& iface, const std::string& ifb_iface) {
    system(("tc qdisc del dev " + iface + " ingress 2>/dev/null").c_str());
    system(("tc qdisc add dev " + iface + " ingress 2>/dev/null").c_str());
    
    std::string filter_cmd = "tc filter add dev " + iface + " parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev " + ifb_iface + " 2>/dev/null";

    if (system(filter_cmd.c_str()) != 0) {
        std::cerr << "[!!] could not redirect ingress traffic to " << ifb_iface << " (non-critical)\n";
    }
}

void BufferbloatShaper::apply_cake(const std::string& target_iface, uint64_t rate_kbit, bool is_upload_direction) {
    std::string cmd = "tc qdisc replace dev " + target_iface + " root cake bandwidth " + std::to_string(rate_kbit) + "kbit diffserv4 triple-isolate nat";

    if (is_upload_direction) {
        cmd += " ack-filter";
    }

    cmd += " 2>/dev/null";

    if (system(cmd.c_str()) != 0) {
        std::cerr << "[!!] could not apply cake shaper on " << target_iface << " (non-critical)\n";
    }
}

void BufferbloatShaper::apply(const std::string& iface, double download_bps, double upload_bps) {
    if (iface.empty()) return;

    std::string ifb_iface = ifb_name_for(iface);

    ensure_ifb_module_loaded();
    create_ifb_device(ifb_iface);
    redirect_ingress_to_ifb(iface, ifb_iface);

    uint64_t download_rate_kbit = shaped_rate_kbit(download_bps);
    uint64_t upload_rate_kbit = shaped_rate_kbit(upload_bps);

    apply_cake(ifb_iface, download_rate_kbit, false);
    apply_cake(iface, upload_rate_kbit, true);
}

void BufferbloatShaper::remove_ingress_redirect(const std::string& iface) {
    system(("tc filter del dev " + iface + " parent ffff: 2>/dev/null").c_str());
    system(("tc qdisc del dev " + iface + " ingress 2>/dev/null").c_str());
}

void BufferbloatShaper::remove_ifb_device(const std::string& ifb_iface) {
    system(("tc qdisc del dev " + ifb_iface + " root 2>/dev/null").c_str());
    system(("ip link set dev " + ifb_iface + " down 2>/dev/null").c_str());
    system(("ip link del " + ifb_iface + " 2>/dev/null").c_str());
}

void BufferbloatShaper::revert(const std::string& iface) {
    if (iface.empty()) return;

    std::string ifb_iface = ifb_name_for(iface);

    system(("tc qdisc del dev " + iface + " root 2>/dev/null").c_str());
    system(("tc qdisc replace dev " + iface + " root fq_codel target 5ms interval 100ms 2>/dev/null").c_str());

    remove_ingress_redirect(iface);
    remove_ifb_device(ifb_iface);
}
}