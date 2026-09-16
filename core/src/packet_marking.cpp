// core/src/packet_marking.cpp
#include "betterconn/packet_marking.hpp"
#include "betterconn/priority_tier.hpp"

#include <array>
#include <cstdlib>
#include <iostream>





namespace betterconn {
namespace {
const std::array<PriorityTier, 4> kAllTiers = {
    PriorityTier::Hot, PriorityTier::Warm, PriorityTier::Cool, PriorityTier::Cold
};


bool ip6tables_available() {
    return system("command -v ip6tables >/dev/null 2>&1") == 0;
}
}


std::vector<std::string> PacketMarking::for_both_families(const std::string& rule_body) {
    std::vector<std::string> rules;

    rules.push_back("iptables -t mangle -A OUTPUT " + rule_body);

    // Leaving IPv6 unmarked would let half of the traffic of a modern desktop bypass the whole priority system
    if (ip6tables_available()) {
        rules.push_back("ip6tables -t mangle -A OUTPUT " + rule_body);
    }

    return rules;
}


std::vector<std::string> PacketMarking::tier_append_rules() {
    std::vector<std::string> rules;

    for (PriorityTier tier : kAllTiers) {
        std::string body = "-m cgroup --path " + std::string(kPriorityCgroupRelativePath) + "/" + tier_cgroup_name(tier) + " -j DSCP --set-dscp " + std::to_string(tier_dscp(tier));

        for (const auto& rule : for_both_families(body)) {
            rules.push_back(rule);
        }
    }

    return rules;
}


std::vector<std::string> PacketMarking::service_append_rules() {
    const std::string mark = " -j DSCP --set-dscp " + std::to_string(tier_dscp(PriorityTier::Hot));

    std::vector<std::string> bodies = {
        "-p udp --dport 53" + mark,
        "-p tcp --dport 53" + mark,
        "-p tcp --tcp-flags SYN,RST,ACK SYN" + mark,
        "-p tcp --tcp-flags SYN,RST,ACK ACK -m length --length 0:128" + mark,
        "-p udp -m length --length 0:256" + mark,
        "-p udp --dport 27000:27030" + mark,
        "-p udp --dport 3478:3481" + mark,
    };

    std::vector<std::string> rules;

    for (const auto& body : bodies) {
        for (const auto& rule : for_both_families(body)) {
            rules.push_back(rule);
        }
    }

    return rules;
}


std::vector<std::string> PacketMarking::append_rules() {
    std::vector<std::string> rules = tier_append_rules();

    // Service rules run last so that handshakes and acknowledgements of a background process still get lifted out of the bulk class instead of inheriting the tier of the process that owns them
    for (const auto& rule : service_append_rules()) {
        rules.push_back(rule);
    }

    return rules;
}


std::string PacketMarking::to_check_command(const std::string& append_rule) {
    std::string command = append_rule;

    auto pos = command.find(" -A ");
    if (pos != std::string::npos) command.replace(pos, 4, " -C ");

    return command + " 2>/dev/null";
}


std::string PacketMarking::to_delete_command(const std::string& append_rule) {
    std::string command = append_rule;

    auto pos = command.find(" -A ");
    if (pos != std::string::npos) command.replace(pos, 4, " -D ");

    return command + " 2>/dev/null";
}


void PacketMarking::load_required_modules() {
    system("modprobe xt_DSCP 2>/dev/null");
    system("modprobe xt_cgroup 2>/dev/null");
    system("modprobe xt_length 2>/dev/null");
}


std::vector<std::string> PacketMarking::idempotent_apply_commands() {
    std::vector<std::string> commands;

    for (const auto& rule : append_rules()) {
        commands.push_back(to_check_command(rule) + " || " + rule + " 2>/dev/null");
    }

    return commands;
}


void PacketMarking::apply() {
    load_required_modules();

    int failed = 0;

    for (const auto& command : idempotent_apply_commands()) {
        if (system(command.c_str()) != 0) ++failed;
    }

    if (failed > 0) {
        std::cerr << "[!!] " << failed << " packet marking rule(s) could not be applied (non-critical)\n";
    }
}


void PacketMarking::revert() {
    for (const auto& rule : append_rules()) {
        system(to_delete_command(rule).c_str());
    }
}
}