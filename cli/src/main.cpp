// cli/src/main.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/status.hpp"
#include "betterconn/storage.hpp"
#include "betterconn/tuner.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

static constexpr const char* kVersion = BETTERCONN_VERSION;





namespace {
volatile sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

void print_help() {
    std::cout << "\nbetterconn - Linux Network Optimizer\n";
    std::cout << "Written by Christian (@pusheandoando)\n";
    std::cout << "\nUsage:\n";
    std::cout << "  betterconn start                         Apply all network optimizations (persists across reboots)\n";
    std::cout << "  betterconn start --skip-warning          Skip the security warning prompt\n";
    std::cout << "  betterconn start --interface <iface>     Apply optimizations on a specific interface\n";
    std::cout << "  betterconn stop                          Revert to original system settings and stop the daemon\n";
    std::cout << "  betterconn status                        Show live connection stats and state\n";
    std::cout << "  betterconn list                          List all available network interfaces\n";
    std::cout << "  betterconn clean                         Remove all betterconn files (requires stop first)\n";
    std::cout << "  betterconn -v, --version                 Show installed version\n";
    std::cout << "  betterconn -h, --help                    Show this message\n";
}

void require_root() {
    if (geteuid() != 0) {
        std::cerr << "[!!] root privileges required\n";
        std::exit(1);
    }
}

bool confirm_security_warning() {
    std::cout << "\n";
    std::cout << "  WARNING: betterconn prioritizes maximizing internet speed and stability over network security.\n";
    std::cout << "\n";
    std::cout << "  Only use it on networks you fully trust (home, personal workplace).\n";
    std::cout << "  Do NOT use it on public networks (airports, hotels, cafes, universities).\n";
    std::cout << "\n";
    std::cout << "  Continue? [y/n]: " << std::flush;

    std::string input;
    std::getline(std::cin, input);
    for (char& c : input) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return input == "y" || input == "yes";
}

void cmd_list() {
    std::filesystem::path net_dir = "/sys/class/net";

    if (!std::filesystem::exists(net_dir)) {
        std::cerr << "[!!] cannot read /sys/class/net\n";
        return;
    }

    std::string default_iface;
    std::ifstream route("/proc/net/route");

    if (route) {
        std::string line;
        std::getline(route, line);

        while (std::getline(route, line)) {
            std::istringstream ss(line);
            std::string iface, dest;
            ss >> iface >> dest;

            if (dest == "00000000") {
                default_iface = iface;
                break;
            }
        }
    }

    std::cout << "\nAvailable network interfaces:\n";

    for (const auto& entry : std::filesystem::directory_iterator(net_dir)) {
        std::string name = entry.path().filename().string();
        bool wifi = std::filesystem::exists(entry.path() / "phy80211");
        bool is_default = (name == default_iface);

        std::cout << "  " << name;
        if (is_default) std::cout << "  [default]";
        if (wifi) std::cout << "  [wifi]";
        std::cout << "\n";
    }

    std::cout << "\n";
}

}

int main(int argc, char* argv[]) {
    if (argc == 1
        || (argc == 2 && std::strcmp(argv[1], "-h") == 0)
        || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (argc == 2
        && (std::strcmp(argv[1], "-v") == 0
            || std::strcmp(argv[1], "--version") == 0)) {
        std::cout << "betterconn " << kVersion << "\n";
        return 0;
    }

    if (argc < 2) {
        std::cerr << "[!!] unknown usage, run: betterconn -h\n";
        return 1;
    }

    std::string cmd = argv[1];

    try {
        if (cmd == "start") {
            require_root();

            bool skip_warning = false;
            std::string iface;

            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];

                if (arg == "--skip-warning") {
                    skip_warning = true;
                } else if (arg == "--interface") {
                    if (i + 1 >= argc) {
                        std::cerr << "[!!] --interface requires an interface name\n";
                        return 1;
                    }
                    iface = argv[++i];
                } else {
                    std::cerr << "[!!] unknown option: " << arg << "\n";
                    std::cerr << "run: betterconn -h\n";
                    return 1;
                }
            }

            if (!skip_warning && !confirm_security_warning()) {
                std::cout << "[!!] aborted\n";
                return 0;
            }

            betterconn::Optimizer opt;
            opt.apply(iface);
            std::cout << "[OK] optimizations applied\n";
            betterconn::Status().print();
        } else if (cmd == "stop") {
            require_root();
            betterconn::Optimizer opt;
            opt.revert();
            std::cout << "[OK] settings restored to original\n";
        } else if (cmd == "status") {
            betterconn::Status().print();
        } else if (cmd == "list") {
            cmd_list();
        } else if (cmd == "clean") {
            require_root();
            betterconn::Cleaner().run();
        } else if (cmd == "daemon") {
            require_root();
            if (!betterconn::Storage::exists("state") ||
                betterconn::Storage::load("state") != "active") {
                std::cerr << "[!!] betterconn is not active, run start first\n";
                return 1;
            }

            std::string iface;
            if (betterconn::Storage::exists("iface")) {
                iface = betterconn::Storage::load("iface");
            }

            signal(SIGTERM, on_signal);
            signal(SIGINT, on_signal);

            betterconn::Tuner tuner;
            tuner.start(iface);

            while (!g_stop) {
                sleep(1);
            }

            tuner.stop();
        } else {
            std::cerr << "[!!] unknown option: " << cmd << "\n";
            std::cerr << "run: betterconn -h\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[!!] " << e.what() << "\n";
        return 1;
    }

    return 0;
}