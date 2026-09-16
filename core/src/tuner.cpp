// core/src/tuner.cpp
#include "betterconn/tuner.hpp"
#include "betterconn/wifi_airtime.hpp"

#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <string>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>





namespace betterconn {
static constexpr int kSampleIntervalMs = 200;
static constexpr int kBufferIntervalMs = 400;
static constexpr int kQdiscIntervalMs = 600;
static constexpr int kWifiIntervalMs = 600;
static constexpr int kRttIntervalMs = 2000;

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


static int qdisc_rtt_bucket_ms(double rtt_ms) {
    if (rtt_ms <= 0.0) return -1;
    if (rtt_ms < 10.0) return 10;
    if (rtt_ms < 20.0) return 20;
    if (rtt_ms < 50.0) return 50;
    if (rtt_ms < 100.0) return 100;

    return 200;
}


void Tuner::set_qdisc_rtt(const std::string& iface, int rtt_ms) {
    if (iface.empty() || rtt_ms <= 0) return;

    // cake derives its AQM target from the configured rtt, so following the measured path rtt keeps the target from being too aggressive on short paths and too slack on long ones
    std::string cmd = "tc qdisc change dev " + iface + " root cake rtt " + std::to_string(rtt_ms) + "ms 2>/dev/null";

    system(cmd.c_str());
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


double Tuner::read_passive_rtt_ms() {
    FILE* p = popen("ss -tin state established 2>/dev/null", "r");
    if (!p) return -1.0;

    std::string out;
    char buf[1024];

    while (fgets(buf, sizeof(buf), p)) out += buf;

    pclose(p);

    const std::string key = "rtt:";
    double best = -1.0;
    size_t search_from = 0;

    while (true) {
        auto pos = out.find(key, search_from);
        if (pos == std::string::npos) break;

        search_from = pos + key.size();

        // "minrtt:" ends with the same three characters, so anything glued to the key is a different field
        if (pos > 0 && std::isalpha(static_cast<unsigned char>(out[pos - 1]))) continue;

        auto value_end = out.find_first_not_of("0123456789.", search_from);
        std::string value = out.substr(search_from, value_end - search_from);

        try {
            double rtt = std::stod(value);

            if (rtt > 0.0 && (best < 0.0 || rtt < best)) best = rtt;
        } catch (...) {
        }
    }

    return best;
}


std::string Tuner::read_default_gateway() {
    std::ifstream f("/proc/net/route");
    if (!f) return "";

    std::string line;
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface, dest, gateway;

        ss >> iface >> dest >> gateway;

        if (dest != "00000000" || gateway.size() != 8) continue;

        unsigned long raw = 0;

        try {
            raw = std::stoul(gateway, nullptr, 16);
        } catch (...) {
            continue;
        }

        if (raw == 0) continue;

        // The route table stores the address in little endian hex, so the first octet is the low byte
        std::ostringstream address;
        address << (raw & 0xff) << "." << ((raw >> 8) & 0xff) << "." << ((raw >> 16) & 0xff) << "." << ((raw >> 24) & 0xff);

        return address.str();
    }

    return "";
}


double Tuner::read_rtt_ms() {
    // Reading the smoothed rtt the kernel already keeps for live sockets measures the real path without adding probe traffic of its own, and it reflects the connections that actually matter
    double passive = read_passive_rtt_ms();
    if (passive > 0.0) return passive;

    std::string gateway = read_default_gateway();
    std::string target = gateway.empty() ? std::string("8.8.8.8") : gateway;

    FILE* p = popen(("ping -c 2 -i 0.2 -W 1 " + target + " 2>/dev/null").c_str(), "r");
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


static constexpr double kPeakDecayPerSample = 0.999;


void Tuner::buffer_loop(std::atomic<bool>& running, SharedSample& s) {
    uint64_t last_seq = 0;
    uint64_t last_written_buf = 0;
    double peak_rx = 0.0;
    double smoothed_rtt = 30.0;
    const double alpha = 0.3;

    while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kBufferIntervalMs));

        uint64_t cur_seq = s.seq.load(std::memory_order_acquire);
        if (cur_seq == last_seq) continue;
        last_seq = cur_seq;

        double rx_bps = s.rx_bps.load(std::memory_order_relaxed);
        double rtt = s.rtt_ms.load(std::memory_order_relaxed);

        // Sizing from the instantaneous rate shrinks the buffers while the link is idle, which then throttles the next download for as long as they take to grow back
        peak_rx = std::max(rx_bps, peak_rx * kPeakDecayPerSample);

        if (rtt > 0.0) smoothed_rtt = alpha * rtt + (1.0 - alpha) * smoothed_rtt;

        uint64_t buf = compute_bdp_buf(peak_rx, smoothed_rtt);

        if (buf == last_written_buf) continue;
        last_written_buf = buf;

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
    (void)scheduler;

    uint64_t last_seq = 0;
    int applied_rtt_ms = -1;

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

        int target_rtt_ms = qdisc_rtt_bucket_ms(rtt);

        if (target_rtt_ms > 0 && target_rtt_ms != applied_rtt_ms) {
            set_qdisc_rtt(iface, target_rtt_ms);
            applied_rtt_ms = target_rtt_ms;
        }
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

    // The airtime queue limits live in debugfs and are reset on every boot, so the daemon reapplies them on each start
    WifiAirtime::apply(iface);

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
    priority_scheduler_.start();
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