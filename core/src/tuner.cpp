// core/src/tuner.cpp
#include "betterconn/tuner.hpp"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>





namespace betterconn {
static constexpr int kSampleIntervalMs = 200;
static constexpr int kBufferIntervalMs = 400;
static constexpr int kQdiscIntervalMs = 600;
static constexpr int kWifiIntervalMs = 600;
static constexpr int kRttIntervalMs = 1000;

static constexpr double kRttHighMs = 55.0;
static constexpr double kRttLowMs = 20.0;
static constexpr double kRssiPoorDbm = -75.0;
static constexpr double kRssiFairDbm = -65.0;
static constexpr double kRetryHighDelta = 5.0;
static constexpr double kRetryLowDelta = 1.0;

static constexpr double kBusyRatioHigh = 0.35;
static constexpr double kBusyRatioLow = 0.15;

static constexpr uint64_t kBufMin = 4194304;
static constexpr uint64_t kBufMax = 33554432;

void Tuner::write_sysctl(const std::string& key, const std::string& value) {
    std::string path = "/proc/sys/";
    
    for (char c : key) path += (c == '.') ? '/' : c;
    std::ofstream f(path);
    
    if (f) f << value << "\n";
}

void Tuner::set_qdisc_target(const std::string& iface, int target_ms) {
    (void)iface;
    (void)target_ms;
}

uint64_t Tuner::compute_bdp_buf(double rx_bps, double rtt_ms) {
    if (rx_bps <= 0.0 || rtt_ms <= 0.0) return 8388608;
    
    double rtt_sec = rtt_ms / 1000.0;
    double bdp = rx_bps * rtt_sec / 8.0;
    double target = bdp * 2.0;
    
    uint64_t buf = static_cast<uint64_t>(target);
    buf = std::max(buf, kBufMin);
    buf = std::min(buf, kBufMax);
    
    uint64_t align = 4096;
    
    buf = (buf + align - 1) & ~(align - 1);
    
    return buf;
}

double Tuner::read_rtt_ms() {
    FILE* p = popen("ping -c 2 -i 0.1 -W 1 8.8.8.8 2>/dev/null", "r");
    if (!p) return -1.0;
    
    std::string out;
    char buf[256];
    
    while (fgets(buf, sizeof(buf), p)) out += buf;
    
    pclose(p);
    auto pos = out.find("rtt min/avg/max");
    if (pos == std::string::npos) return -1.0;
    
    auto eq = out.find("= ", pos);
    if (eq == std::string::npos) return -1.0;
    
    auto s = eq + 2;
    auto sl1 = out.find('/', s);
    auto sl2 = (sl1 != std::string::npos) ? out.find('/', sl1 + 1) : std::string::npos;
    if (sl2 == std::string::npos) return -1.0;
    
    try { return std::stod(out.substr(sl1 + 1, sl2 - sl1 - 1)); } catch (...) { return -1.0; }
}

static uint64_t read_iface_rx(const std::string& iface) {
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

static uint64_t read_iface_tx(const std::string& iface) {
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

double Tuner::read_rx_bps(const std::string& iface, int ms) {
    uint64_t before = read_iface_rx(iface);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    uint64_t after = read_iface_rx(iface);

    return static_cast<double>(after - before) * 8.0 / (ms / 1000.0);
}

double Tuner::read_tx_bps(const std::string& iface, int ms) {
    uint64_t before = read_iface_tx(iface);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    uint64_t after = read_iface_tx(iface);

    return static_cast<double>(after - before) * 8.0 / (ms / 1000.0);
}

double Tuner::read_rssi_dbm(const std::string& iface) {
    std::ifstream f("/proc/net/wireless");
    if (!f) return 0.0;
    
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string name = line.substr(0, colon);
        
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);
        
        if (name != iface) continue;
        
        std::istringstream ss(line.substr(colon + 1));
        double status, link, level;
        ss >> status >> link >> level;
        
        return level;
    }

    return 0.0;
}

double Tuner::read_tx_retries(const std::string& iface) {
    std::ifstream f("/proc/net/wireless");
    if (!f) return 0.0;
    
    std::string line;
    std::getline(f, line);
    std::getline(f, line);
    
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string name = line.substr(0, colon);
        
        auto trim = name.find_first_not_of(' ');
        if (trim != std::string::npos) name = name.substr(trim);
        
        if (name != iface) continue;
        std::istringstream ss(line.substr(colon + 1));
        
        double status, link, level, noise, nwid, crypt, frag, retry;
        ss >> status >> link >> level >> noise >> nwid >> crypt >> frag >> retry;
        
        return retry;
    }
    
    return 0.0;
}

bool Tuner::is_wifi(const std::string& iface) {
    return std::filesystem::exists("/sys/class/net/" + iface + "/phy80211");
}

void Tuner::sampler_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s) {
    const bool wifi = is_wifi(iface);
    double prev_retries = 0.0;

    while (running.load(std::memory_order_relaxed)) {
        uint64_t rx0 = read_iface_rx(iface);
        uint64_t tx0 = read_iface_tx(iface);

        std::this_thread::sleep_for(std::chrono::milliseconds(kSampleIntervalMs));

        uint64_t rx1 = read_iface_rx(iface);
        uint64_t tx1 = read_iface_tx(iface);

        double dt = kSampleIntervalMs / 1000.0;
        double rx_bps = static_cast<double>(rx1 - rx0) * 8.0 / dt;
        double tx_bps = static_cast<double>(tx1 - tx0) * 8.0 / dt;

        s.rx_bps.store(rx_bps, std::memory_order_relaxed);
        s.tx_bps.store(tx_bps, std::memory_order_relaxed);

        if (wifi) {
            s.rssi_dbm.store(read_rssi_dbm(iface), std::memory_order_relaxed);
            double cur_retries = read_tx_retries(iface);
            s.tx_retries.store(cur_retries - prev_retries, std::memory_order_relaxed);
            prev_retries = cur_retries;
        }

        s.seq.fetch_add(1, std::memory_order_release);
    }
}

void Tuner::buffer_loop(std::atomic<bool>& running, SharedSample& s) {
    uint64_t last_seq = 0;
    double smoothed_rx = 0.0;
    double smoothed_rtt = 30.0;
    const double alpha = 0.3;

    while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kBufferIntervalMs));

        uint64_t cur_seq = s.seq.load(std::memory_order_acquire);
        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        double rx_bps = s.rx_bps.load(std::memory_order_relaxed);
        double rtt = s.rtt_ms.load(std::memory_order_relaxed);

        smoothed_rx = alpha * rx_bps + (1.0 - alpha) * smoothed_rx;
        if (rtt > 0.0) smoothed_rtt = alpha * rtt + (1.0 - alpha) * smoothed_rtt;

        uint64_t buf = compute_bdp_buf(smoothed_rx, smoothed_rtt);

        write_sysctl("net.core.rmem_max", std::to_string(buf));
        write_sysctl("net.core.wmem_max", std::to_string(buf));

        uint64_t buf_def = buf / 4;
        buf_def = std::max(buf_def, uint64_t(1048576));

        write_sysctl("net.core.rmem_default", std::to_string(buf_def));
        write_sysctl("net.core.wmem_default", std::to_string(buf_def));

        std::string tcp_vec = "4096 " + std::to_string(buf_def) + " " + std::to_string(buf);

        write_sysctl("net.ipv4.tcp_rmem", tcp_vec);
        write_sysctl("net.ipv4.tcp_wmem", tcp_vec);
    }
}

void Tuner::qdisc_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s, SurveyMonitor& survey, PriorityScheduler& scheduler) {
    (void)iface;
    (void)scheduler;
    uint64_t last_seq = 0;

    while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kQdiscIntervalMs));

        uint64_t cur_seq = s.seq.load(std::memory_order_acquire);
        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        double rtt = s.rtt_ms.load(std::memory_order_relaxed);
        double rx_bps = s.rx_bps.load(std::memory_order_relaxed);

        std::string notsent = "131072";
        std::string budget = "500";

        if (rtt > 0.0 && rtt > kRttHighMs) {
            notsent = "65536";
            budget = "400";
        } else if (rtt > 0.0 && rtt < kRttLowMs) {
            notsent = "131072";
            budget = "600";
        } else if (rx_bps > 80.0 * 1024.0 * 1024.0 * 8.0) {
            notsent = "262144";
            budget = "700";
        }

        if (survey.has_data()) {
            double busy_ratio = survey.busy_ratio();

            if (busy_ratio > kBusyRatioHigh) {
                notsent = "65536";
            }
        }

        write_sysctl("net.ipv4.tcp_notsent_lowat", notsent);
        write_sysctl("net.core.netdev_budget", budget);
    }
}

void Tuner::wifi_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s) {
    if (!is_wifi(iface)) return;

    uint64_t last_seq = 0;

    while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kWifiIntervalMs));

        uint64_t cur_seq = s.seq.load(std::memory_order_acquire);
        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        double rssi = s.rssi_dbm.load(std::memory_order_relaxed);
        double retry_delta = s.tx_retries.load(std::memory_order_relaxed);
        double rtt = s.rtt_ms.load(std::memory_order_relaxed);

        bool poor_signal = (rssi != 0.0 && rssi < kRssiPoorDbm);
        bool fair_signal = (rssi != 0.0 && rssi < kRssiFairDbm);
        bool high_retry = (retry_delta > kRetryHighDelta);
        bool low_retry = (retry_delta < kRetryLowDelta);
        bool high_rtt = (rtt > 0.0 && rtt > kRttHighMs);
        bool low_rtt = (rtt > 0.0 && rtt < kRttLowMs);

        if (poor_signal || high_retry) {
            write_sysctl("net.ipv4.tcp_autocorking", "1");
            write_sysctl("net.core.netdev_budget_usecs", "12000");
        } else if (fair_signal || high_rtt) {
            write_sysctl("net.ipv4.tcp_autocorking", "0");
            write_sysctl("net.core.netdev_budget_usecs", "8000");
        } else if (low_retry && low_rtt) {
            write_sysctl("net.ipv4.tcp_autocorking", "0");
            write_sysctl("net.core.netdev_budget_usecs", "6000");
        } else {
            write_sysctl("net.ipv4.tcp_autocorking", "0");
            write_sysctl("net.core.netdev_budget_usecs", "8000");
        }
    }
}

void Tuner::rtt_loop(std::atomic<bool>& running, SharedSample& s) {
    while (running.load(std::memory_order_relaxed)) {
        double rtt = read_rtt_ms();

        if (rtt > 0.0) {
            s.rtt_ms.store(rtt, std::memory_order_relaxed);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kRttIntervalMs));
    }
}

void Tuner::start(const std::string& iface) {
    running_.store(true, std::memory_order_relaxed);

    sampler_thread_ = std::thread([iface, this]() {
        sampler_loop(iface, running_, sample_);
    });

    buffer_thread_ = std::thread([this]() {
        buffer_loop(running_, sample_);
    });

    qdisc_thread_ = std::thread([iface, this]() {
        qdisc_loop(iface, running_, sample_, survey_monitor_, priority_scheduler_);
    });

    wifi_thread_ = std::thread([iface, this]() {
        wifi_loop(iface, running_, sample_);
    });

    rtt_thread_ = std::thread([this]() {
        rtt_loop(running_, sample_);
    });

    survey_monitor_.start(iface);
    priority_scheduler_.start(iface);
}

void Tuner::stop() {
    running_.store(false, std::memory_order_relaxed);

    if (sampler_thread_.joinable()) sampler_thread_.join();
    if (buffer_thread_.joinable()) buffer_thread_.join();
    if (qdisc_thread_.joinable()) qdisc_thread_.join();
    if (wifi_thread_.joinable()) wifi_thread_.join();
    if (rtt_thread_.joinable()) rtt_thread_.join();

    survey_monitor_.stop();
    priority_scheduler_.stop();
}
}