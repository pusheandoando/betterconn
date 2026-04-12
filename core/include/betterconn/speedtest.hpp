// core/include/betterconn/speedtest.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
class Speedtest {
public:
    void run();

private:
    struct Sample {
        double download_bps;
        double ping_ms;
        double ping_under_load_ms;
    };

    static Sample measure();
    static double measure_download_once();
    static double measure_ping();
    static double measure_ping_under_load();
    static std::string fmt_speed(double bps);
    static std::string fmt_ms(double ms);
    static std::string fmt_pct(double before, double after, bool lower_better);
};
}