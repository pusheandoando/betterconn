// core/include/betterconn/survey_monitor.hpp
#pragma once

#include <atomic>
#include <string>
#include <thread>





namespace betterconn {
struct SurveySample {
    double busy_time;
    double active_time;
    double tx_time;
    double noise_dbm;
    bool valid;
};

class SurveyMonitor {
public:
    void start(const std::string& iface);
    void stop();

    double busy_ratio() const;
    double noise_dbm() const;
    bool has_data() const;

private:
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};

    std::atomic<double> busy_ratio_{-1.0};
    std::atomic<double> noise_dbm_{0.0};
    std::atomic<bool> has_data_{false};

    static void monitor_loop(const std::string& iface, std::atomic<bool>& running,
                              std::atomic<double>& busy_ratio, std::atomic<double>& noise_dbm,
                              std::atomic<bool>& has_data);
    static bool is_wifi(const std::string& iface);
    static SurveySample read_active_channel_survey(const std::string& iface);
};
}