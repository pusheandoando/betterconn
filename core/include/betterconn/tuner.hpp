// core/include/betterconn/tuner.hpp
#pragma once

#include <atomic>
#include <string>
#include <thread>





namespace betterconn {
struct NetSample {
    double rtt_ms;
    double rx_bps;
    double tx_bps;
    double rssi_dbm;
    double tx_retries;
    bool valid;
};

class Tuner {
public:
    void start(const std::string& iface);
    void stop();

private:
    std::thread sampler_thread_;
    std::thread buffer_thread_;
    std::thread qdisc_thread_;
    std::thread wifi_thread_;
    std::atomic<bool> running_{false};

    struct alignas(64) SharedSample {
        std::atomic<double> rtt_ms{-1.0};
        std::atomic<double> rx_bps{0.0};
        std::atomic<double> tx_bps{0.0};
        std::atomic<double> rssi_dbm{0.0};
        std::atomic<double> tx_retries{0.0};
        std::atomic<uint64_t> seq{0};
    };

    SharedSample sample_;

    static void sampler_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s);
    static void buffer_loop(std::atomic<bool>& running, SharedSample& s);
    static void qdisc_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s);
    static void wifi_loop(const std::string& iface, std::atomic<bool>& running, SharedSample& s);

    static double read_rtt_ms();
    static double read_rx_bps(const std::string& iface, int ms);
    static double read_tx_bps(const std::string& iface, int ms);
    static double read_rssi_dbm(const std::string& iface);
    static double read_tx_retries(const std::string& iface);
    static bool is_wifi(const std::string& iface);
    static void write_sysctl(const std::string& key, const std::string& value);
    static void set_qdisc_target(const std::string& iface, int target_ms);
    static uint64_t compute_bdp_buf(double rx_bps, double rtt_ms);
};
}