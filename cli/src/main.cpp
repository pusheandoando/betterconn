// cli/src/main.cpp
#include "betterconn/cleaner.hpp"
#include "betterconn/optimizer.hpp"
#include "betterconn/status.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>





namespace {
void print_help() {
    std::cout << "\nbetterconn - Linux Network Optimizer\n";
    std::cout << "by Christian (@pusheandoando)\n";
    std::cout << "\nUsage:\n";
    std::cout << "  betterconn --start      Apply all network optimizations (persists across reboots)\n";
    std::cout << "  betterconn --stop       Revert to original system settings\n";
    std::cout << "  betterconn --status     Show live connection stats and state\n";
    std::cout << "  betterconn --clean      Remove all betterconn files from the system (requires --stop first)\n";
    std::cout << "  betterconn --help       Show this message\n";
}

void require_root() {
    if (geteuid() != 0) {
        std::cerr << "[!!] root privileges required\n";
        std::exit(1);
    }
}
}

int main(int argc, char* argv[]) {
    if (argc == 1
        || (argc == 2 && std::strcmp(argv[1], "-h") == 0)
        || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (argc != 2) {
        std::cerr << "[!!] unknown usage, run: betterconn --help\n";
        return 1;
    }

    std::string cmd = argv[1];

    try {
        if (cmd == "--start") {
            require_root();
            betterconn::Optimizer opt;
            opt.apply();
            std::cout << "[OK] optimizations applied\n";
            betterconn::Status().print();
        } else if (cmd == "--stop") {
            require_root();
            betterconn::Optimizer opt;
            opt.revert();
            std::cout << "[OK] settings restored to original\n";
        } else if (cmd == "--status") {
            betterconn::Status().print();
        } else if (cmd == "--clean") {
            require_root();
            betterconn::Cleaner().run();
        } else {
            std::cerr << "[!!] unknown option: " << cmd << "\n";
            std::cerr << "run: betterconn --help\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[!!] " << e.what() << "\n";
        return 1;
    }

    return 0;
}