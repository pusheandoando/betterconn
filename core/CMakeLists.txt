# core/CMakeLists.txt
add_library(betterconn_core
    src/storage.cpp
    src/optimizer.cpp
    src/status.cpp
    src/cleaner.cpp
    src/tuner.cpp
    src/survey_monitor.cpp
    src/focus_priority.cpp
    src/window_focus_backend.cpp
    src/proc_activity.cpp
    src/context_persistence.cpp
    src/priority_scheduler.cpp
    src/focus_decay_tracker.cpp
    src/network_info.cpp
    src/bufferbloat_shaper.cpp
)
target_include_directories(betterconn_core PUBLIC include)

find_package(Threads REQUIRED)

target_link_libraries(betterconn_core PRIVATE compiler_options)
target_link_libraries(betterconn_core PUBLIC Threads::Threads)