// core/src/storage.cpp
#include "betterconn/storage.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>





namespace betterconn {
std::filesystem::path Storage::dir() {
    const char* home = std::getenv("HOME");
    
    if (!home) {
        throw std::runtime_error("[!!] HOME environment variable not set");
    }

    return std::filesystem::path(home) / ".betterconn";
}

void Storage::ensure_dir() {
    std::filesystem::create_directories(dir());
}

void Storage::save(const std::string& name, const std::string& content) {
    ensure_dir();
    std::ofstream f(dir() / name);
    
    if (!f) {
        throw std::runtime_error("[!!] cannot write storage file: " + name);
    }
    
    f << content;
}

std::string Storage::load(const std::string& name) {
    std::ifstream f(dir() / name);
    
    if (!f) {
        throw std::runtime_error("[!!] cannot read storage file: " + name);
    }
    
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Storage::exists(const std::string& name) {
    return std::filesystem::exists(dir() / name);
}

void Storage::remove_file(const std::string& name) {
    std::filesystem::remove(dir() / name);
}
}