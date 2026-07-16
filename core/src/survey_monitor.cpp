// core/src/survey_monitor.cpp
#include "betterconn/survey_monitor.hpp"

#include <string>
#include <cstdio>
#include <sstream>
#include <filesystem>





namespace betterconn {
static constexpr int kSurveyIntervalMs = 500;

bool SurveyMonitor::is_wifi(const std::string& iface) {
    return std::filesystem::exists("/sys/class/net/" + iface + "/phy80211");
}

SurveySample SurveyMonitor::read_active_channel_survey(const std::string& iface) {
    SurveySample sample{-1.0, -1.0, -1.0, 0.0, false};

    std::string cmd = "iw dev " + iface + " survey dump 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return sample;

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    pclose(p);

    auto block_start = output.find("frequency:");
    bool found_active_block = false;

    while (block_start != std::string::npos) {
        auto line_end = output.find('\n', block_start);
        std::string freq_line = output.substr(block_start, line_end - block_start);

        if (freq_line.find("[in use]") != std::string::npos) {
            found_active_block = true;
            break;
        }

        block_start = output.find("frequency:", line_end);
    }

    if (!found_active_block) return sample;

    auto block_end = output.find("frequency:", block_start + 1);
    std::string block = output.substr(block_start, block_end - block_start);

    auto parse_field = [&block](const std::string& key) -> double {
        auto pos = block.find(key);
        if (pos == std::string::npos) return -1.0;

        std::istringstream ss(block.substr(pos + key.size()));
        double value;
        ss >> value;
        
        return ss.fail() ? -1.0 : value;
    };

    double noise = parse_field("noise:");
    double active_time = parse_field("channel active time:");
    double busy_time = parse_field("channel busy time:");
    double tx_time = parse_field("channel transmit time:");

    if (noise != -1.0) sample.noise_dbm = noise;

    sample.active_time = active_time;
    sample.busy_time = busy_time;
    sample.tx_time = tx_time;
    sample.valid = (active_time != -1.0 && busy_time != -1.0);

    return sample;
}

void SurveyMonitor::monitor_loop(const std::string& iface, std::atomic<bool>& running,
                                  std::atomic<double>& busy_ratio, std::atomic<double>& noise_dbm,
                                  std::atomic<bool>& has_data) {
    if (!is_wifi(iface)) return;

    double prev_active_time = -1.0;
    double prev_busy_time = -1.0;
    double prev_tx_time = -1.0;

    while (running.load(std::memory_order_relaxed)) {
        SurveySample sample = read_active_channel_survey(iface);

        if (sample.valid) {
            noise_dbm.store(sample.noise_dbm, std::memory_order_relaxed);

            if (prev_active_time >= 0.0 && sample.active_time >= prev_active_time) {
                double delta_active = sample.active_time - prev_active_time;
                double delta_busy = sample.busy_time - prev_busy_time;

                bool tx_supported = (prev_tx_time >= 0.0 && sample.tx_time >= 0.0);
                double delta_tx = tx_supported ? (sample.tx_time - prev_tx_time) : 0.0;

                double contention_active = delta_active - delta_tx;
                double contention_busy = delta_busy - delta_tx;

                if (contention_active > 0.0 && contention_busy >= 0.0) {
                    busy_ratio.store(contention_busy / contention_active, std::memory_order_relaxed);
                    has_data.store(true, std::memory_order_relaxed);
                }
            }

            prev_active_time = sample.active_time;
            prev_busy_time = sample.busy_time;
            prev_tx_time = sample.tx_time;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kSurveyIntervalMs));
    }
}

void SurveyMonitor::start(const std::string& iface) {
    running_.store(true, std::memory_order_relaxed);

    monitor_thread_ = std::thread([iface, this]() {
        monitor_loop(iface, running_, busy_ratio_, noise_dbm_, has_data_);
    });
}

void SurveyMonitor::stop() {
    running_.store(false, std::memory_order_relaxed);
    
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

double SurveyMonitor::busy_ratio() const {
    return busy_ratio_.load(std::memory_order_relaxed);
}

double SurveyMonitor::noise_dbm() const {
    return noise_dbm_.load(std::memory_order_relaxed);
}

bool SurveyMonitor::has_data() const {
    return has_data_.load(std::memory_order_relaxed);
}
}