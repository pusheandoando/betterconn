// core/include/betterconn/bufferbloat_shaper.hpp
#pragma once

#include <string>
#include <cstdint>





namespace betterconn {
struct MeasuredThroughput {
    double download_bps;
    double upload_bps;
    bool valid;
};

class BufferbloatShaper {
public:
    static MeasuredThroughput measure_throughput(const std::string& iface, int sample_ms = 2000);

    static void apply(const std::string& iface, double download_bps, double upload_bps);
    static void revert(const std::string& iface);

    static uint64_t shaped_rate_kbit(double measured_bps);

private:
    static constexpr double kShapingFraction = 0.875;
    static constexpr uint64_t kMinShapedRateKbit = 512;

    static std::string ifb_name_for(const std::string& iface);
    static void ensure_ifb_module_loaded();
    static void create_ifb_device(const std::string& ifb_iface);
    static void redirect_ingress_to_ifb(const std::string& iface, const std::string& ifb_iface);
    static void remove_ingress_redirect(const std::string& iface);
    static void remove_ifb_device(const std::string& ifb_iface);
    static void apply_cake(const std::string& target_iface, uint64_t rate_kbit, bool is_upload_direction);
};
}