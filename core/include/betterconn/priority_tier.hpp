// core/include/betterconn/priority_tier.hpp
#pragma once





namespace betterconn {
enum class PriorityTier {
    Cold,
    Cool,
    Warm,
    Hot,
};


constexpr const char* kPriorityCgroupBase = "/sys/fs/cgroup/betterconn_priority";
constexpr const char* kPriorityCgroupRelativePath = "betterconn_priority";


const char* tier_cgroup_name(PriorityTier tier);
int tier_dscp(PriorityTier tier);
}