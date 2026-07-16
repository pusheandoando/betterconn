// core/include/betterconn/storage.hpp
#pragma once

#include <string>
#include <filesystem>





namespace betterconn {
class Storage {
public:
    static std::filesystem::path dir();
    static void ensure_dir();
    static void save(const std::string& name, const std::string& content);
    static std::string load(const std::string& name);
    static bool exists(const std::string& name);
    static void remove_file(const std::string& name);
};
}