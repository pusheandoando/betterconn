// core/src/priority_tier.cpp
#include "betterconn/priority_tier.hpp"





namespace betterconn {
const char* tier_cgroup_name(PriorityTier tier) {
    switch (tier) {
        case PriorityTier::Hot: return "hot";
        case PriorityTier::Warm: return "warm";
        case PriorityTier::Cool: return "cool";
        case PriorityTier::Cold:
        default: return "cold";
    }
}


// mac80211 derives the 802.11 user priority from the DS field, so the same value that picks the CAKE tin also picks the WMM access category. EF stays below CS6/CS7 on purpose: AC_VO disables A-MPDU aggregation on several drivers, which would cost the focused process far more throughput than it gains in latency.
int tier_dscp(PriorityTier tier) {
    switch (tier) {
        case PriorityTier::Hot: return 46;
        case PriorityTier::Warm: return 32;
        case PriorityTier::Cool: return 0;
        case PriorityTier::Cold:
        default: return 8;
    }
}
}