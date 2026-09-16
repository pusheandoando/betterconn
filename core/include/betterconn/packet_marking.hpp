// core/include/betterconn/packet_marking.hpp
#pragma once

#include <string>
#include <vector>





namespace betterconn {
class PacketMarking {
public:
    static void apply();
    static void revert();

    static void load_required_modules();
    static std::vector<std::string> idempotent_apply_commands();

private:
    static std::vector<std::string> append_rules();
    static std::vector<std::string> tier_append_rules();
    static std::vector<std::string> service_append_rules();
    static std::vector<std::string> for_both_families(const std::string& rule_body);
    static std::string to_check_command(const std::string& append_rule);
    static std::string to_delete_command(const std::string& append_rule);
};
}