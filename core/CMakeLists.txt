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
    src/app_classifier.cpp
    src/priority_score.cpp
    src/context_persistence.cpp
    src/priority_scheduler.cpp
)

target_include_directories(betterconn_core PUBLIC include)
target_link_libraries(betterconn_core PRIVATE compiler_options)