// core/include/betterconn/status.hpp
#pragma once

#include <string>
#include <utility>





namespace betterconn {
class Status {
public:
    void print() const;

private:
    static std::string detect_interface();
    static std::pair<double, double> measure_speed(const std::string& iface);
    static double measure_ping(const std::string& host);
    static std::string read_sysctl(const std::string& key);
};
}