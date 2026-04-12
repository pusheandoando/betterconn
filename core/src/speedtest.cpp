// core/src/speedtest.cpp
#include "betterconn/speedtest.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>





namespace betterconn {
std::string Speedtest::fmt_speed(double bps) {
    if (bps < 0.0) return "n/a";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    if (bps >= 1e6) { ss << bps / 1e6 << " MB/s"; }
    else if (bps >= 1e3) { ss << bps / 1e3 << " KB/s"; }
    else { ss << bps << " B/s"; }
    return ss.str();
}

std::string Speedtest::fmt_ms(double ms) {
    if (ms < 0.0) return "n/a";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << ms << " ms";
    return ss.str();
}

std::string Speedtest::fmt_pct(double before, double after, bool lower_better) {
    if (before <= 0.0 || after < 0.0) return "n/a";
    double delta = lower_better
        ? (before - after) / before * 100.0
        : (after - before) / before * 100.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (delta >= 0.0) ss << "+";
    ss << delta << "%";
    return ss.str();
}

double Speedtest::measure_download_once() {
    const char* url = "https://speed.cloudflare.com/__down?bytes=10000000";
    std::string cmd = std::string("curl -o /dev/null -s -w \"%{speed_download}\""
                                  " --max-time 30 -L \"") + url + "\" 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1.0;
    char buf[64];
    std::string out;
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    
    try {
        double v = std::stod(out);
        return v > 0.0 ? v : -1.0;
    } catch (...) {
        return -1.0;
    }
}

double Speedtest::measure_ping() {
    std::string cmd = "ping -c 5 -i 0.2 -W 2 8.8.8.8 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1.0;
    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    pclose(p);
    
    auto pos = output.find("rtt min/avg/max");
    if (pos == std::string::npos) return -1.0;
    auto eq = output.find("= ", pos);
    if (eq == std::string::npos) return -1.0;
    auto start = eq + 2;
    auto s1 = output.find('/', start);
    if (s1 == std::string::npos) return -1.0;
    auto s2 = output.find('/', s1 + 1);
    if (s2 == std::string::npos) return -1.0;
    
    try {
        return std::stod(output.substr(s1 + 1, s2 - s1 - 1));
    } catch (...) {
        return -1.0;
    }
}

double Speedtest::measure_ping_under_load() {
    const char* script_path = "/tmp/.betterconn_load_test.sh";
    const char* output_path = "/tmp/.betterconn_load_ping.txt";

    {
        std::ofstream f(script_path);
        if (!f) return -1.0;
        // Download a large file in the background to saturate the uplink/downlink.
        // 50 MB with a 25s cap guarantees load for the full ping measurement window.
        f << "#!/bin/sh\n"
          << "curl -o /dev/null -s --max-time 25 -L "
             "\"https://speed.cloudflare.com/__down?bytes=52428800\" "
             ">/dev/null 2>&1 &\n"
          << "CPID=$!\n"
          << "sleep 0.5\n"
          << "ping -c 10 -i 0.5 -W 2 8.8.8.8 2>/dev/null\n"
          << "kill $CPID 2>/dev/null\n"
          << "wait $CPID 2>/dev/null\n";
    }
    system(("chmod +x " + std::string(script_path)).c_str());
    system((std::string(script_path) + " > " + output_path + " 2>/dev/null").c_str());

    std::string output;
    {
        std::ifstream f(output_path);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            output = ss.str();
        }
    }

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
    std::filesystem::remove(output_path, ec);

    auto pos = output.find("rtt min/avg/max");
    if (pos == std::string::npos) return -1.0;
    auto eq = output.find("= ", pos);
    if (eq == std::string::npos) return -1.0;
    auto start = eq + 2;
    auto s1 = output.find('/', start);
    if (s1 == std::string::npos) return -1.0;
    auto s2 = output.find('/', s1 + 1);
    if (s2 == std::string::npos) return -1.0;
    
    try {
        return std::stod(output.substr(s1 + 1, s2 - s1 - 1));
    } catch (...) {
        return -1.0;
    }
}

Speedtest::Sample Speedtest::measure() {
    Sample s;

    std::vector<double> dl_runs;
    for (int i = 0; i < 3; ++i) {
        std::cout << "  [..] download " << (i + 1) << "/3..." << std::flush;
        double v = measure_download_once();
        dl_runs.push_back(v);
        std::cout << " " << fmt_speed(v) << "\n";
    }
    std::vector<double> valid;
    for (double v : dl_runs) {
        if (v > 0.0) valid.push_back(v);
    }
    std::sort(valid.begin(), valid.end());
    s.download_bps = valid.empty() ? -1.0 : valid[valid.size() / 2];

    std::cout << "  [..] idle ping (5 pings)..." << std::flush;
    s.ping_ms = measure_ping();
    std::cout << " " << fmt_ms(s.ping_ms) << "\n";

    std::cout << "  [..] ping under load (10 pings, download saturating link)..." << std::flush;
    s.ping_under_load_ms = measure_ping_under_load();
    std::cout << " " << fmt_ms(s.ping_under_load_ms) << "\n";

    return s;
}

void Speedtest::run() {
    if (system("command -v curl >/dev/null 2>&1") != 0) {
        throw std::runtime_error("curl is required for --speedtest: sudo apt install curl");
    }

    bool was_active = Storage::exists("state") && Storage::load("state") == "active";

    std::cout << "\nbetterconn speedtest\n";
    std::cout << "--------------------\n";
    std::cout << "Metrics:\n";
    std::cout << "  download      3 runs of 10 MB via Cloudflare, median reported\n";
    std::cout << "  idle ping     5 pings to 8.8.8.8\n";
    std::cout << "  ping/load     10 pings to 8.8.8.8 while a 50 MB download saturates the link\n";
    std::cout << "                this is the primary bufferbloat indicator\n";
    std::cout << "Estimated duration: ~60 seconds.\n\n";

    if (was_active) {
        std::cout << "[..] disabling optimizations for baseline...\n";
        try {
            Optimizer().revert();
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("failed to disable optimizations: ") + e.what());
        }
    }

    std::cout << "[..] baseline (no optimizations):\n";
    Sample base = measure();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "[..] enabling optimizations...\n";
    try {
        Optimizer().apply();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("failed to apply optimizations: ") + e.what());
    }

    std::cout << "[..] optimized:\n";
    Sample opt = measure();

    if (!was_active) {
        std::cout << "[..] disabling optimizations (restoring pre-test state)...\n";
        
        try {
            Optimizer().revert();
        } catch (const std::exception& e) {
            std::cerr << "[!!] could not restore state: " << e.what() << "\n";
        }
    }

    std::cout << "\nresults\n";
    std::cout << "-------\n";
    std::cout << "download (median):    "
              << fmt_speed(base.download_bps) << "  ->  "
              << fmt_speed(opt.download_bps)
              << "  (" << fmt_pct(base.download_bps, opt.download_bps, false) << ")\n";
    std::cout << "ping idle:            "
              << fmt_ms(base.ping_ms) << "  ->  "
              << fmt_ms(opt.ping_ms)
              << "  (" << fmt_pct(base.ping_ms, opt.ping_ms, true) << ")\n";
    std::cout << "ping under load:      "
              << fmt_ms(base.ping_under_load_ms) << "  ->  "
              << fmt_ms(opt.ping_under_load_ms)
              << "  (" << fmt_pct(base.ping_under_load_ms, opt.ping_under_load_ms, true) << ")\n";

    std::cout << "\nstate after test: " << (was_active ? "ACTIVE" : "INACTIVE") << "\n\n";
}
}