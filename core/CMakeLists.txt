# core/CMakeLists.txt
add_library(betterconn_core
    src/storage.cpp
    src/optimizer.cpp
    src/status.cpp
    src/cleaner.cpp
    src/tuner.cpp
    src/survey_monitor.cpp
    src/window_focus_backend.cpp
    src/session_environment.cpp
    src/proc_activity.cpp
    src/context_persistence.cpp
    src/priority_tier.cpp
    src/priority_scheduler.cpp
    src/packet_marking.cpp
    src/focus_decay_tracker.cpp
    src/network_info.cpp
    src/irq_affinity.cpp
    src/wifi_airtime.cpp
    src/profile_selector.cpp
    src/audio_activity_monitor.cpp
    src/network_flow_classifier.cpp
    src/system_defaults.cpp
)
target_include_directories(betterconn_core PUBLIC include)

find_package(Threads REQUIRED)

target_link_libraries(betterconn_core PRIVATE compiler_options)
target_link_libraries(betterconn_core PUBLIC Threads::Threads)