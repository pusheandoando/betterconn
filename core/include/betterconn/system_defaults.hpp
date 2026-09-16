// core/include/betterconn/system_defaults.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
struct SysctlDefault {
    std::string key;
    std::string value;
};


class SystemDefaults {
public:
    static bool persistence_present();
    static void remove_persistence();
    static void restore_kernel_sysctl_defaults();
    static void reload_configured_sysctls();
    static void unload_optimizer_modules();
    static void regenerate_initramfs();

private:
    static std::vector<std::string> persistence_paths();
    static std::vector<SysctlDefault> kernel_defaults();
    static bool command_available(const std::string& command);
    static void write_sysctl(const std::string& key, const std::string& value);
};
}