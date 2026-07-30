// cli/src/main.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/colors.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/status.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/network_info.hpp"
#include "betterconn/tuner.hpp"
#include "betterconn/storage.hpp"

#include <string>
#include <cctype>
#include <sstream>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <stdexcept>
#include <filesystem>





static constexpr const char* kVersion = BETTERCONN_VERSION;

namespace {
volatile sig_atomic_t g_stop = 0;

constexpr int kRebootDelaySeconds = 10;

void on_signal(int) {
    g_stop = 1;
}

void reboot_with_countdown() {
    for (int remaining = kRebootDelaySeconds; remaining >= 1; --remaining) {
        std::cout << "\r" CLR_YELLOW "[..] rebooting system in " << remaining << " seconds...   " CLR_RESET << std::flush;
        sleep(1);
    }

    std::cout << "\n";
    sync();
    system("systemctl reboot");
}

void print_help() {
    std::cout << "\n" CLR_BOLD CLR_LWHITE "betterconn " << kVersion << CLR_RESET " - Linux Network Optimizer\n";
    std::cout << CLR_WHITE "Written by Christian (@pusheandoando)\n" CLR_RESET;
    std::cout << "\n" CLR_BOLD "Usage:\n" CLR_RESET;
    std::cout << "  " CLR_CYAN "betterconn start" CLR_RESET "                         Apply all network optimizations (persists across reboots)\n";
    std::cout << "  " CLR_CYAN "betterconn start --skip-warning" CLR_RESET "          Skip the security warning prompt\n";
    std::cout << "  " CLR_CYAN "betterconn start --interface <iface>" CLR_RESET "     Apply optimizations on a specific interface\n";
    std::cout << "  " CLR_CYAN "betterconn stop" CLR_RESET "                          Revert to original system settings and stop the daemon\n";
    std::cout << "  " CLR_CYAN "betterconn status" CLR_RESET "                        Show live connection stats and state\n";
    std::cout << "  " CLR_CYAN "betterconn list" CLR_RESET "                          List all available network interfaces\n";
    std::cout << "  " CLR_CYAN "betterconn clean" CLR_RESET "                         Remove all betterconn files (requires stop first)\n";
    std::cout << "  " CLR_CYAN "betterconn -v, --version" CLR_RESET "                 Show installed version\n";
    std::cout << "  " CLR_CYAN "betterconn -h, --help" CLR_RESET "                    Show this message\n";
    std::cout << "  " CLR_CYAN "betterconn network" CLR_RESET "                       Show info for the currently active network\n";
}

void require_root() {
    if (geteuid() != 0) {
        std::cerr << CLR_LRED "[!!] root privileges required\n" CLR_RESET;
        std::exit(1);
    }
}

bool confirm_security_warning() {
    std::cout << "\n";
    std::cout << CLR_YELLOW "  WARNING: betterconn prioritizes maximizing internet speed and stability over network security.\n" CLR_RESET;
    std::cout << "\n";
    std::cout << CLR_WHITE "  Only use it on networks you fully trust (home, personal workplace).\n";
    std::cout << "  Do NOT use it on public networks (airports, hotels, cafes, universities).\n" CLR_RESET;
    std::cout << "\n";
    std::cout << CLR_YELLOW "  betterconn runs a background thread that polls the active window every 500ms\n";
    std::cout << "  via xdotool on X11, or via the compositor's IPC on supported Wayland compositors\n";
    std::cout << "  (Sway/wlroots, Hyprland), and assigns the process currently in focus to a\n";
    std::cout << "  high-priority network class. This means the process ID of whatever application\n";
    std::cout << "  you are actively using is continuously read while betterconn is running. This\n";
    std::cout << "  happens entirely on your own machine, nothing leaves your system, and it stops\n";
    std::cout << "  as soon as you run betterconn stop.\n" CLR_RESET;
    std::cout << "\n";
    std::cout << CLR_LRED "  betterconn will reboot the system after applying changes for all settings to take effect.\n";
    std::cout << "  Make sure you have saved all open work and closed all applications before continuing\n";
    std::cout << "  to avoid any data loss.\n" CLR_RESET;
    std::cout << "\n";
    std::cout << CLR_WHITE "  Continue? [y/n]: " CLR_RESET << std::flush;

    std::string input;
    std::getline(std::cin, input);
    
    for (char& c : input) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    
    return input == "y" || input == "yes";
}

void print_stop_reboot_warning() {
    std::cout << "\n";
    std::cout << CLR_LRED "  betterconn will reboot the system for all reverted settings to take effect.\n";
    std::cout << "  Make sure you have saved all open work and closed all applications before continuing\n";
    std::cout << "  to avoid any data loss.\n" CLR_RESET;
    std::cout << "\n";
    std::cout << CLR_WHITE "  Continue? [y/n]: " CLR_RESET << std::flush;
}

std::string detect_default_interface() {
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

    return default_iface;
}

void cmd_list() {
    const std::string net_dir = "/sys/class/net";

    if (!std::filesystem::exists(net_dir)) {
        std::cerr << CLR_LRED "[!!] cannot read /sys/class/net\n" CLR_RESET;
        return;
    }

    std::string default_iface = detect_default_interface();

    std::cout << "\n" CLR_BOLD CLR_LWHITE "Available network interfaces:\n" CLR_RESET;

    for (const auto& entry : std::filesystem::directory_iterator(net_dir)) {
        std::string name = entry.path().filename().string();
        bool wifi = std::filesystem::exists(entry.path() / "phy80211");
        bool is_default = (name == default_iface);

        if (is_default) {
            std::cout << "  " CLR_BOLD CLR_LGREEN << name << CLR_RESET;
        } else {
            std::cout << "  " CLR_CYAN << name << CLR_RESET;
        }

        if (is_default) std::cout << CLR_LGREEN "  [default]" CLR_RESET;

        if (wifi) std::cout << CLR_LCYAN "  [wifi]" CLR_RESET;
        
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
        std::cout << CLR_LWHITE "betterconn " CLR_LGREEN << kVersion << CLR_RESET "\n";
        
        return 0;
    }

    if (argc < 2) {
        std::cerr << CLR_LRED "[!!] unknown usage, run: betterconn -h\n" CLR_RESET;
        
        return 1;
    }

    std::string cmd = argv[1];

    try {
        if (cmd == "start") {
            require_root();

            betterconn::Optimizer opt;

            if (opt.is_active()) {
                std::cerr << CLR_LRED "[!!] betterconn is already active, run stop first\n" CLR_RESET;
                
                return 1;
            }

            bool skip_warning = false;
            std::string iface;

            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];

                if (arg == "--skip-warning") {
                    skip_warning = true;
                } else if (arg == "--interface") {
                    if (i + 1 >= argc) {
                        std::cerr << CLR_LRED "[!!] --interface requires an interface name\n" CLR_RESET;
                        
                        return 1;
                    }
                    iface = argv[++i];
                } else {
                    std::cerr << CLR_LRED "[!!] unknown option: " << arg << "\n" CLR_RESET;
                    std::cerr << CLR_WHITE "run: betterconn -h\n" CLR_RESET;
                    
                    return 1;
                }
            }

            if (!skip_warning && !confirm_security_warning()) {
                std::cout << CLR_YELLOW "[!!] aborted\n" CLR_RESET;
                
                return 0;
            }

            opt.apply(iface);
            std::cout << CLR_LGREEN "[OK] optimizations applied\n" CLR_RESET;
            betterconn::Status().print();

            reboot_with_countdown();
        } else if (cmd == "stop") {
            require_root();

            betterconn::Optimizer opt;

            if (!opt.is_active()) {
                std::cerr << CLR_LRED "[!!] betterconn is not active\n" CLR_RESET;
                
                return 1;
            }

            print_stop_reboot_warning();

            std::string input;
            std::getline(std::cin, input);
            for (char& c : input) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (input != "y" && input != "yes") {
                std::cout << CLR_YELLOW "[!!] aborted\n" CLR_RESET;
                
                return 0;
            }

            opt.revert();
            std::cout << CLR_LGREEN "[OK] settings restored to original\n" CLR_RESET;

            sync();
            betterconn::Cleaner().run();
            sleep(2);

            reboot_with_countdown();
        } else if (cmd == "status") {
            require_root();
            betterconn::Status().print();
        } else if (cmd == "list") {
            cmd_list();
        } else if (cmd == "clean") {
            require_root();
            
            if (betterconn::Storage::exists("state") && betterconn::Storage::load("state") == "active") {
                std::cerr << CLR_LRED "[!!] betterconn is active, run stop first\n" CLR_RESET;
                
                return 1;
            }

            betterconn::Cleaner().run();
        } else if (cmd == "daemon") {
            require_root();
            
            if (!betterconn::Storage::exists("state") ||
                betterconn::Storage::load("state") != "active") {
                std::cerr << CLR_LRED "[!!] betterconn is not active, run start first\n" CLR_RESET;
                
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
        } else if (cmd == "network") {
            bool show_help = false;
            bool show_secrets = false;
            bool has_interface_toggle = false;
            bool interface_toggle_on = false;

            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];

                if (arg == "--info") {
                    continue;
                } else if (arg == "--secrets") {
                    show_secrets = true;
                } else if (arg == "--interface") {
                    if (i + 1 >= argc) {
                        std::cerr << CLR_LRED "[!!] --interface requires 'on' or 'off'\n" CLR_RESET;
                        
                        return 1;
                    }

                    std::string value = argv[++i];

                    if (value == "on") {
                        has_interface_toggle = true;
                        interface_toggle_on = true;
                    } else if (value == "off") {
                        has_interface_toggle = true;
                        interface_toggle_on = false;
                    } else {
                        std::cerr << CLR_LRED "[!!] --interface requires 'on' or 'off'\n" CLR_RESET;
                        
                        return 1;
                    }
                } else if (arg == "-h" || arg == "--help") {
                    show_help = true;
                } else {
                    std::cerr << CLR_LRED "[!!] unknown option: " << arg << "\n" CLR_RESET;
                    std::cerr << CLR_WHITE "run: betterconn network -h\n" CLR_RESET;
                    
                    return 1;
                }
            }

            if (show_help) {
                betterconn::NetworkInfo::print_help();
                
                return 0;
            }

            require_root();

            std::string iface = detect_default_interface();

            if (has_interface_toggle) {
                if (interface_toggle_on) {
                    betterconn::NetworkInfo::force_network_on(iface);
                } else {
                    betterconn::NetworkInfo::force_network_off(iface);
                }
            } else if (show_secrets) {
                betterconn::NetworkInfo::print_secrets(iface);
            } else {
                betterconn::NetworkInfo::print_info(iface);
            }
        } else {
            std::cerr << CLR_LRED "[!!] unknown option: " << cmd << "\n" CLR_RESET;
            std::cerr << CLR_WHITE "run: betterconn -h\n" CLR_RESET;
            
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << CLR_LRED "[!!] " << e.what() << "\n" CLR_RESET;
        
        return 1;
    }

    return 0;
}