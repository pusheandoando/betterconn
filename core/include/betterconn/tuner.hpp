// core/include/betterconn/tuner.hpp
#pragma once

#include <atomic>
#include <string>
#include <thread>





namespace betterconn {
class Tuner {
public:
    void start(const std::string& iface);
    void stop();

private:
    std::thread thread_;
    std::atomic<bool> running_{false};

    static void loop(const std::string& iface, std::atomic<bool>& running);

    static double read_rtt_ms();
    static double read_tx_retries(const std::string& iface);
    static double read_rx_bps(const std::string& iface);
    static double read_rssi_dbm(const std::string& iface);
    static bool is_wifi(const std::string& iface);
    static void write_sysctl(const std::string& key, const std::string& value);
    static void set_qdisc_target(const std::string& iface, int target_ms);
};
}